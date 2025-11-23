// UPDATED udp_server.c (by Rob Rogacion - 11/23/2025 - 5:21 PM)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define MaxBufferSize 1024
#pragma comment(lib,"ws2_32.lib")

#define RESEND_TIMEOUT_MS 500
#define MAX_RETRIES 3

bool spectActive = false;
SOCKADDR_IN SpectatorADDR;
int SpectatorAddrSize = sizeof(SpectatorADDR);

typedef struct {
    bool occupied;
    char data[MaxBufferSize * 2];
    int len;
    int seq;
    int retries;
    DWORD last_sent_ms;
    struct sockaddr_in dest;
    int dest_len;
} PendingMsg;

PendingMsg pending;

void init_pending() {
    pending.occupied = false;
    pending.len = 0;
    pending.seq = 0;
    pending.retries = 0;
    pending.last_sent_ms = 0;
    pending.dest_len = 0;
}

char *get_message_type(char *message) {
    char *message_type = strstr(message, "message_type: ");
    if (message_type) {
        return message_type + 14;
    }
    return NULL;
}

int get_seq_from_message(const char *message) {
    const char *p = strstr(message, "seq:");
    if (!p) return -1;
    int s = -1;
    if (sscanf(p, "seq: %d", &s) == 1) return s;
    return -1;
}

void build_ack_message(char *out, int seq) {
    sprintf(out, "message_type: ACK\nseq: %d", seq);
}

int raw_sendto(SOCKET s, const char *buf, int len, const struct sockaddr_in *to, int tolen) {
    return sendto(s, buf, len, 0, (SOCKADDR*)to, tolen);
}

// send sequenced message (one outstanding slot)
int send_sequenced_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    if (pending.occupied) {
        // server only supports one outstanding message at a time in this simple implementation.
        // Caller should handle the case (we choose to drop or queue externally).
        printf("[SERVER] Cannot send now: another message waiting for ACK (seq=%d)\n", pending.seq);
        return -1;
    }
    static int next_seq = 1;
    int myseq = next_seq++;
    char composed[MaxBufferSize * 2];
    snprintf(composed, sizeof(composed), "%s\nseq: %d", payload, myseq);

    pending.occupied = true;
    pending.seq = myseq;
    pending.retries = 0;
    pending.last_sent_ms = GetTickCount();
    pending.len = (int)strlen(composed);
    if (pending.len >= (int)sizeof(pending.data)) pending.len = (int)sizeof(pending.data)-1;
    memcpy(pending.data, composed, pending.len);
    pending.data[pending.len] = '\0';
    pending.dest = *dest;
    pending.dest_len = dest_len;

    int sent = raw_sendto(sock, pending.data, pending.len, &pending.dest, pending.dest_len);
    if (sent == SOCKET_ERROR) {
        printf("[SERVER] sendto() error: %d\n", WSAGetLastError());
    } else {
        printf("[SERVER] Sent sequenced (seq=%d) to client\n", pending.seq);
    }
    return 0;
}

void send_ack(SOCKET sock, int seq, const struct sockaddr_in *to, int tolen) {
    char ack[128];
    build_ack_message(ack, seq);
    int sent = raw_sendto(sock, ack, (int)strlen(ack), to, tolen);
    if (sent == SOCKET_ERROR) {
        printf("[SERVER] Failed to send ACK for seq %d: %d\n", seq, WSAGetLastError());
    } else {
        printf("[SERVER] Sent ACK seq=%d\n", seq);
    }
}

// forward to spectator (reliably if possible)
void spectatorUpdateReliable(const char *updateMessage, SOCKET socket) {
    if (!spectActive) return;
    struct sockaddr_in dest = SpectatorADDR;
    int destlen = SpectatorAddrSize;

    // try to send sequenced message (will be retried in main loop)
    int r = send_sequenced_message(socket, updateMessage, &dest, destlen);
    if (r != 0) {
        // if cannot send (pending occupied), print and drop for now
        printf("[SERVER] spectator update dropped (pending occupied)\n");
    } else {
        printf("[SERVER] Spectator update queued (reliable)\n");
    }
}

int main() {
    WSADATA wsa;
    char receive[MaxBufferSize * 2];
    char response[MaxBufferSize * 2];
    struct sockaddr_in SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);

    printf("Server starting...\n");

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed! Error code: %d\n", WSAGetLastError());
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket == INVALID_SOCKET) {
        printf("Server: socket() error: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(server_socket, (SOCKADDR *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("Server: bind() failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }

    init_pending();

    printf("Server ready. Listening on port 9002...\n");

    bool running = true;

    while (running) {
        // Use select with small timeout so we can check retransmission timers
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10 ms

        int activity = select(0, &readfds, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR) {
            printf("select() error\n");
            break;
        }

        if (FD_ISSET(server_socket, &readfds)) {
            memset(receive, 0, sizeof(receive));
            int ByteReceived = recvfrom(server_socket, receive, sizeof(receive)-1, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
            if (ByteReceived <= 0) continue;
            receive[ByteReceived] = '\0';

            char *msg = get_message_type(receive);
            if (!msg) continue;

            // If it's an ACK, parse seq and clear pending if matches
            if (!strncmp(msg, "ACK", 3)) {
                int ack_seq = get_seq_from_message(receive);
                if (ack_seq >= 0 && pending.occupied && ack_seq == pending.seq) {
                    printf("[SERVER] Received ACK seq=%d: clearing pending\n", ack_seq);
                    pending.occupied = false;
                } else {
                    printf("[SERVER] Received ACK seq=%d (no matching pending)\n", ack_seq);
                }
                continue;
            }

            // If message has a seq value, send ACK back immediately
            int incoming_seq = get_seq_from_message(receive);
            if (incoming_seq >= 0) {
                send_ack(server_socket, incoming_seq, &SenderAddr, SenderAddrSize);
            }

            // Now handle application-level message types
            if (strncmp(msg, "exit", 4) == 0) {
                printf("[SERVER] Exit command received.\n");
                running = false;
            }
            else if (strcmp(msg, "HANDSHAKE_REQUEST") == 0) {
                printf("[SERVER] Handshake Request received.\n");
                int seed = 12345;
                sprintf(response, "message_type: HANDSHAKE_RESPONSE\nseed: %d", seed);
                // send sequenced (so client can ACK)
                send_sequenced_message(server_socket, response, &SenderAddr, SenderAddrSize);
            }
            else if (strcmp(msg, "SPECTATOR_REQUEST") == 0) {
                printf("[SERVER] Spectator Request received.\n");
                SpectatorADDR = SenderAddr;
                spectActive = true;
                sprintf(response, "message_type: SPECTATOR_RESPONSE");
                send_sequenced_message(server_socket, response, &SpectatorADDR, SpectatorAddrSize);
            }
            else {
                // Forward message to spectator (reliably)
                spectatorUpdateReliable(receive, server_socket);
                printf("\n[SERVER] Received (%d bytes): %s\n", ByteReceived, receive);
            }
        }

        // Retransmission logic for pending outgoing message (server side)
        if (pending.occupied) {
            DWORD now = GetTickCount();
            if ((now - pending.last_sent_ms) >= RESEND_TIMEOUT_MS) {
                if (pending.retries >= MAX_RETRIES) {
                    printf("[SERVER] Message seq=%d failed after %d retries. Dropping pending.\n", pending.seq, pending.retries);
                    // drop pending (server chooses not to terminate entirely)
                    pending.occupied = false;
                } else {
                    int sent = raw_sendto(server_socket, pending.data, pending.len, &pending.dest, pending.dest_len);
                    pending.retries++;
                    pending.last_sent_ms = now;
                    if (sent == SOCKET_ERROR) {
                        printf("[SERVER] Retransmit seq=%d failed: %d\n", pending.seq, WSAGetLastError());
                    } else {
                        printf("[SERVER] Retransmit seq=%d (retry %d)\n", pending.seq, pending.retries);
                    }
                }
            }
        }
    }

    closesocket(server_socket);
    WSACleanup();
    printf("Server shut down.\n");
    return 0;
}
