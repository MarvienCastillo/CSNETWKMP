// reliability.c
#define _CRT_SECURE_NO_WARNINGS
#include "reliability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    SockAddrIn addr;
    uint32_t seq;
    char *data;          // owned buffer containing enveloped message (header + payload)
    int data_len;
    int retries;
    unsigned long last_sent_ms;
    bool in_use;
} OutstandingMsg;

static OutstandingMsg outstanding[REL_MAX_OUTSTANDING];
static volatile LONG next_seq = 1;
static CRITICAL_SECTION rel_cs;
static SOCKET rel_sock = INVALID_SOCKET;
static HANDLE rel_thread = NULL;
static volatile bool rel_thread_run = false;
static rel_connection_failed_cb on_conn_failed = NULL;

// helper: Get tick in ms
static unsigned long rel_now_ms() {
    return GetTickCount();
}

static int sockaddr_equal(const SockAddrIn *a, const SockAddrIn *b) {
    return (a->sin_addr.s_addr == b->sin_addr.s_addr) && (a->sin_port == b->sin_port);
}

// low level send
static int rel_sendto(SockAddrIn *dest, const char *buf, int len) {
    int sent = sendto(rel_sock, buf, len, 0, (const struct sockaddr*)dest, sizeof(SockAddrIn));
    return (sent == SOCKET_ERROR) ? -1 : sent;
}

static int rel_enqueue_outstanding(SockAddrIn *dest, uint32_t seq, char *buf, int len) {
    EnterCriticalSection(&rel_cs);
    int idx = -1;
    for (int i = 0; i < REL_MAX_OUTSTANDING; ++i) {
        if (!outstanding[i].in_use) { idx = i; break; }
    }
    if (idx == -1) {
        LeaveCriticalSection(&rel_cs);
        return -1;
    }
    outstanding[idx].in_use = true;
    outstanding[idx].addr = *dest;
    outstanding[idx].seq = seq;
    outstanding[idx].data = buf; // ownership
    outstanding[idx].data_len = len;
    outstanding[idx].retries = 0;
    outstanding[idx].last_sent_ms = rel_now_ms();
    LeaveCriticalSection(&rel_cs);
    return 0;
}

static OutstandingMsg* rel_find_outstanding(SockAddrIn *from, uint32_t seq) {
    for (int i = 0; i < REL_MAX_OUTSTANDING; ++i) {
        if (outstanding[i].in_use && outstanding[i].seq == seq && sockaddr_equal(&outstanding[i].addr, from))
            return &outstanding[i];
    }
    return NULL;
}

static void rel_remove_outstanding(OutstandingMsg *om) {
    if (!om) return;
    EnterCriticalSection(&rel_cs);
    if (om->in_use) {
        free(om->data);
        om->data = NULL;
        om->in_use = false;
    }
    LeaveCriticalSection(&rel_cs);
}

// worker thread: retransmit timed-out messages
static DWORD WINAPI rel_thread_func(LPVOID param) {
    (void)param;
    while (rel_thread_run) {
        unsigned long now = rel_now_ms();
        EnterCriticalSection(&rel_cs);
        for (int i = 0; i < REL_MAX_OUTSTANDING; ++i) {
            if (!outstanding[i].in_use) continue;
            OutstandingMsg *om = &outstanding[i];
            if ((int)(now - om->last_sent_ms) >= REL_TIMEOUT_MS) {
                if (om->retries >= REL_MAX_RETRIES) {
                    // connection failed for this peer (invoke callback outside CS)
                    SockAddrIn peer = om->addr;
                    char dbgaddr[64];
                    sprintf(dbgaddr, "%s:%u", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
                    printf("[RELIABILITY] peer %s retries exhausted for seq=%u\n", dbgaddr, (unsigned int)om->seq);
                    free(om->data);
                    om->data = NULL;
                    om->in_use = false;
                    LeaveCriticalSection(&rel_cs);
                    if (on_conn_failed) on_conn_failed(&peer);
                    EnterCriticalSection(&rel_cs);
                    // restart loop since array might have changed
                } else {
                    // retransmit
                    int sent = rel_sendto(&om->addr, om->data, om->data_len);
                    om->retries++;
                    om->last_sent_ms = rel_now_ms();
                    printf("[RELIABILITY] retransmit seq=%u retry=%d sent=%d to %s:%u\n",
                           (unsigned int)om->seq, om->retries, sent,
                           inet_ntoa(om->addr.sin_addr), ntohs(om->addr.sin_port));
                }
            }
        }
        LeaveCriticalSection(&rel_cs);
        Sleep(50);
    }
    return 0;
}

int rel_init(SOCKET sock) {
    if (sock == INVALID_SOCKET) return -1;
    rel_sock = sock;
    InitializeCriticalSection(&rel_cs);
    memset(outstanding, 0, sizeof(outstanding));
    next_seq = 1;
    rel_thread_run = true;
    rel_thread = CreateThread(NULL, 0, rel_thread_func, NULL, 0, NULL);
    if (!rel_thread) {
        rel_thread_run = false;
        DeleteCriticalSection(&rel_cs);
        return -1;
    }
    return 0;
}

void rel_shutdown(void) {
    rel_thread_run = false;
    if (rel_thread) {
        WaitForSingleObject(rel_thread, 2000);
        CloseHandle(rel_thread);
        rel_thread = NULL;
    }
    EnterCriticalSection(&rel_cs);
    for (int i = 0; i < REL_MAX_OUTSTANDING; ++i) {
        if (outstanding[i].in_use) {
            free(outstanding[i].data);
            outstanding[i].data = NULL;
            outstanding[i].in_use = false;
        }
    }
    LeaveCriticalSection(&rel_cs);
    DeleteCriticalSection(&rel_cs);
    rel_sock = INVALID_SOCKET;
}

void rel_set_connection_failed_callback(rel_connection_failed_cb cb) {
    on_conn_failed = cb;
}

// Build envelope:
// DATA:
// "SEQ:<n>\nTYPE:DATA\n\n<payload bytes...>"
// ACK:
// "TYPE:ACK\nACK:<n>\n\n"
int rel_send(SockAddrIn *dest, const char *payload, int payload_len) {
    if (!dest || !payload || payload_len <= 0) return -1;
    // build header
    char header[128];
    uint32_t seq = (uint32_t)InterlockedIncrement(&next_seq);
    int header_len = snprintf(header, sizeof(header), "SEQ:%u\nTYPE:DATA\n\n", (unsigned int)seq);
    int total_len = header_len + payload_len;
    char *buf = (char*)malloc(total_len);
    if (!buf) return -1;
    memcpy(buf, header, header_len);
    memcpy(buf + header_len, payload, payload_len);

    // send immediately
    int sent = rel_sendto(dest, buf, total_len);
    if (sent < 0) {
        free(buf);
        return -1;
    }

    // enqueue for retransmit; buf ownership moved into queue
    if (rel_enqueue_outstanding(dest, seq, buf, total_len) != 0) {
        // queue full
        free(buf);
        return -1;
    }
    return 0;
}

static int rel_send_ack(SockAddrIn *dest, uint32_t seq_to_ack) {
    char ackbuf[128];
    int ack_len = snprintf(ackbuf, sizeof(ackbuf), "TYPE:ACK\nACK:%u\n\n", (unsigned int)seq_to_ack);
    int sent = rel_sendto(dest, ackbuf, ack_len);
    if (sent < 0) return -1;
    return 0;
}

// Process incoming datagram. If ACK -> remove outstanding. If DATA -> send ack and copy payload.
// out_buf/out_len: caller provides buffer and value pointed by out_len is buffer size; on return it is payload length.
int rel_process_incoming(char *raw_buf, int raw_len, SockAddrIn *from, char *out_buf, int *out_len) {
    if (!raw_buf || raw_len <= 0) return 0;
    // find header end (\n\n)
    int header_end = -1;
    for (int i = 0; i < raw_len - 1; ++i) {
        if (raw_buf[i] == '\n' && raw_buf[i+1] == '\n') { header_end = i + 2; break; }
    }
    if (header_end == -1) {
        // no envelope found - treat as raw app payload
        if (out_buf && out_len) {
            int copy_len = (*out_len < raw_len) ? *out_len : raw_len;
            memcpy(out_buf, raw_buf, copy_len);
            *out_len = copy_len;
            return 1;
        }
        return 0;
    }
    // parse header lines
    char *hdr = (char*)malloc(header_end + 1);
    memcpy(hdr, raw_buf, header_end);
    hdr[header_end] = 0;
    uint32_t seq_val = 0;
    int is_ack = 0;
    char *line = strtok(hdr, "\n");
    while (line) {
        if (_strnicmp(line, "TYPE:", 5) == 0) {
            char *v = line + 5;
            while (*v == ' ') v++;
            if (_stricmp(v, "ACK") == 0) is_ack = 1;
        } else if (_strnicmp(line, "ACK:", 4) == 0) {
            seq_val = (uint32_t)atoi(line + 4);
        } else if (_strnicmp(line, "SEQ:", 4) == 0) {
            seq_val = (uint32_t)atoi(line + 4);
        }
        line = strtok(NULL, "\n");
    }
    free(hdr);

    if (is_ack) {
        // remove outstanding entry matching seq/from
        EnterCriticalSection(&rel_cs);
        OutstandingMsg *om = rel_find_outstanding(from, seq_val);
        if (om) {
            free(om->data);
            om->data = NULL;
            om->in_use = false;
            printf("[RELIABILITY] received ACK for seq=%u from %s:%u\n",
                   (unsigned int)seq_val, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        }
        LeaveCriticalSection(&rel_cs);
        return 0;
    } else {
        // DATA message: send ACK and deliver payload
        if (seq_val == 0) {
            // malformed; deliver raw payload after header_end
            int payload_len = raw_len - header_end;
            if (out_buf && out_len) {
                int copy_len = (payload_len > *out_len) ? *out_len : payload_len;
                memcpy(out_buf, raw_buf + header_end, copy_len);
                *out_len = copy_len;
                return 1;
            }
            return 0;
        }
        // send ACK back (best-effort)
        rel_send_ack(from, seq_val);

        int payload_len = raw_len - header_end;
        if (out_buf && out_len) {
            int copy_len = (payload_len > *out_len) ? *out_len : payload_len;
            memcpy(out_buf, raw_buf + header_end, copy_len);
            *out_len = copy_len;
            return 1;
        }
        return 0;
    }
}

void rel_force_terminate_for_peer(SockAddrIn *peer) {
    EnterCriticalSection(&rel_cs);
    for (int i = 0; i < REL_MAX_OUTSTANDING; ++i) {
        if (outstanding[i].in_use && sockaddr_equal(&outstanding[i].addr, peer)) {
            free(outstanding[i].data);
            outstanding[i].data = NULL;
            outstanding[i].in_use = false;
        }
    }
    LeaveCriticalSection(&rel_cs);
}
