// udp_host.c
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <windows.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")

#define REL_MAX_OUTSTANDING 256
#define REL_TIMEOUT_MS 500   // 500 milliseconds timeout
#define REL_MAX_RETRIES 3

typedef struct sockaddr_in SockAddrIn;

typedef void (*rel_connection_failed_cb)(SockAddrIn *peer);

// Initialize reliability with the bound UDP socket (socket already created and bound).
// Returns 0 on success.
int rel_init(SOCKET sock);

// Shutdown reliability layer (stop worker thread, free resources)
void rel_shutdown(void);

// Send a reliable (sequenced) message to dest. The function copies payload so caller may free.
// Returns 0 on success, -1 on failure (e.g., internal allocation error)
int rel_send(SockAddrIn *dest, const char *payload, int payload_len);

// Process an incoming raw UDP datagram. If it's a DATA message, this function will send ACK and
// copy the application payload into out_buf (up to *out_len). Returns 1 if there is app data to deliver,
// 0 if the datagram was an ACK or nothing to deliver, -1 on parse error.
int rel_process_incoming(char *raw_buf, int raw_len, SockAddrIn *from, char *out_buf, int *out_len);

// Set callback invoked when a peer exhausts retries (connection failure)
void rel_set_connection_failed_callback(rel_connection_failed_cb cb);

// Forcibly drop outstanding messages for a peer (optional)
void rel_force_terminate_for_peer(SockAddrIn *peer);

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

#define MaxBufferSize 1024

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32]; // P2P or BROADCAST
    char pokemonName[64];
    StatBoosts boosts;
} BattleSetupData;

bool is_handshake_done = false;
bool is_battle_started = false;
bool is_game_over = false;
bool VERBOSE_MODE = false;

void vprint(const char *fmt, ...) {
    if (!VERBOSE_MODE) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// Base64 encode/decode — minimal portable implementation
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *src, size_t len) {
    char *out, *pos;
    const unsigned char *end, *in;
    size_t olen = 4*((len+2)/3);
    out = malloc(olen+1);
    if (!out) return NULL;
    pos = out;
    end = src + len;
    in = src;

    while (end - in >= 3) {
        *pos++ = b64_table[in[0] >> 2];
        *pos++ = b64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *pos++ = b64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *pos++ = b64_table[in[2] & 0x3f];
        in += 3;
    }

    if (end - in) {
        *pos++ = b64_table[in[0] >> 2];
        if (end - in == 1) {
            *pos++ = b64_table[(in[0] & 0x03) << 4];
            *pos++ = '=';
        } else {
            *pos++ = b64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *pos++ = b64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        }
        *pos++ = '=';
    }

    *pos = '\0';
    return out;
}

unsigned char *base64_decode(const char *src, size_t *out_len) {
    int table[256];
    int i;

    for (i = 0; i < 256; i++) 
        table[i] = -1;  // initialize all to -1
    for (i = 0; i < 64; i++) 
        table[(unsigned char)b64_table[i]] = i;

    size_t len = strlen(src);
    unsigned char *out = malloc(len);
    if (!out) return NULL;

    int val = 0, valb = -8;
    size_t pos = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = src[i];
        if (c == '=' || c > 127) continue;
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out[pos++] = (val >> valb) & 0xFF;
            valb -= 8;
        }
    }
    *out_len = pos;
    return out;
}

void saveSticker(const char *b64, const char *sender) {
    size_t out_len;
    unsigned char *data = base64_decode(b64, &out_len);
    if (!data) {
        printf("[ERROR] Failed to decode Base64 sticker.\n");
        return;
    }

    char filename[256];
    sprintf(filename, "%s_sticker.png", b64);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("[ERROR] Cannot save sticker.\n");
        free(data);
        return;
    }

    fwrite(data, 1, out_len, fp);
    fclose(fp);
    free(data);

    printf("[STICKER] Sticker received from %s \u2192 saved as %s\n", sender, filename);
    
    vprint("[VERBOSE] Sticker saved from %s, bytes=%zu\n", sender, out_len);
}

char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
    return NULL;
}

void processChatMessage(char *msg) {
    char sender[64], type[16];

    char *p_sender = strstr(msg, "sender_name: ");
    char *p_type   = strstr(msg, "content_type: ");
    if (!p_sender || !p_type) return;

    sscanf(p_sender, "sender_name: %63[^\n]", sender);
    sscanf(p_type, "content_type: %15[^\n]", type);

    if (!strcmp(type, "TEXT")) {
        char *p_text = strstr(msg, "message_text: ");
        if (!p_text) return;

        char text[512];
        sscanf(p_text, "message_text: %511[^\n]", text);
        printf("[CHAT] %s: %s\n", sender, text);

    } else if (!strcmp(type, "STICKER")) {
        char *p_data = strstr(msg, "sticker_data: ");
        if (!p_data) return;

        char *b64 = p_data + strlen("sticker_data: ");
        saveSticker(b64, sender);
    }
}

void clean_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len = strlen(str);
    }
}

void processBattleSetup(char *receive,BattleSetupData *setup) {
    char *comm_mode_ptr = strstr(receive, "communication_mode: ");
    char *pokemon_name_ptr = strstr(receive, "pokemon_name: ");
    char *atk = strstr(receive,"\"special_attack_uses\": ");
    char *def = strstr(receive, "\"special_defense_uses\": ");

    sscanf(comm_mode_ptr, "communication_mode: %31[^\n]", setup->communicationMode);
    sscanf(pokemon_name_ptr, "pokemon_name: %63[^\n]", setup->pokemonName);
    sscanf(atk, "\"special_attack_uses\": %d", &setup->boosts.specialAttack);
    sscanf(def, "\"special_defense_uses\": %d", &setup->boosts.specialDefense);
    printf("[HOST] Parsed BATTLE_SETUP: mode=%s, pokemon=%s, atk=%d, def=%d\n",
           setup->communicationMode, setup->pokemonName,
           setup->boosts.specialAttack, setup->boosts.specialDefense);
}

void on_conn_failed_handler(SockAddrIn *peer) {
    char dbgaddr[64];
    sprintf(dbgaddr, "%s:%u", inet_ntoa(peer->sin_addr), ntohs(peer->sin_port));
    printf("\n[HOST] CRITICAL: Connection failed for peer %s - Retries exhausted.\n", dbgaddr);
    // In a real application, you'd mark the peer as disconnected and handle cleanup.
}

int main() {
    WSADATA wsa;
    SOCKET socket_network;
    SockAddrIn server_address, from_joiner;
    int from_len = sizeof(from_joiner);

    BattleSetupData setup;
    BattleSetupData joiner_setup;

    char raw_receive[MaxBufferSize * 2]; // For raw UDP packet
    char app_payload[MaxBufferSize * 2]; // For application-level payload after reliability layer processing
    int app_payload_len;

    char buffer[MaxBufferSize];
    char full_message[MaxBufferSize * 2];
    int seed = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    socket_network = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_network == INVALID_SOCKET) {
        printf("socket() failed.\n");
        WSACleanup();
        return 1;
    }

    // Bind host socket to 0.0.0.0 so it can receive packets
    memset(&from_joiner, 0, sizeof(from_joiner));
    from_joiner.sin_family = AF_INET;
    from_joiner.sin_port = htons(9002);
    from_joiner.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(socket_network, (SOCKADDR*)&from_joiner, sizeof(from_joiner)) == SOCKET_ERROR) {
        printf("bind() failed with error %d\n", WSAGetLastError());
        closesocket(socket_network);
        WSACleanup();
        return 1;
    }
    
    // Initialize reliability layer
    if (rel_init(socket_network) != 0) {
        printf("[HOST] Reliability layer initialization failed.\n");
        closesocket(socket_network);
        WSACleanup();
        return 1;
    }
    rel_set_connection_failed_callback(on_conn_failed_handler);

    // unicast sending address (will be populated on first incoming packet)
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    // server_address.sin_port is set later on first packet, but is typically 9002 for joiner
    server_address.sin_port = htons(9002); 

    printf("Host listening on 0.0.0.0:9002. Waiting for handshake request...\n");

    while (!is_game_over) {
        // to read incoming messages with timeout
        fd_set readfds;
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(socket_network, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        
        int activity = select(0, &readfds, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR) {
            printf("select() error\n");
            break;
        }

        if (FD_ISSET(socket_network, &readfds)) {
            memset(raw_receive, 0, sizeof(raw_receive));
            from_len = sizeof(from_joiner);
            int byte_received = recvfrom(socket_network, raw_receive, sizeof(raw_receive) - 1, 0,
                                         (SOCKADDR*)&from_joiner, &from_len);
            
            if(byte_received == SOCKET_ERROR) {
                printf("recvfrom() failed: %d\n", WSAGetLastError());
                continue;
            }

            printf("[HOST] Raw packet received from %s:%d\n",
                   inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port));
            
            // Update the joiner's address for sending reliable messages later
            server_address.sin_addr = from_joiner.sin_addr;
            server_address.sin_port = from_joiner.sin_port; // Note: from_joiner.sin_port is already in network byte order

            if (byte_received > 0) {
                // Process raw packet through reliability layer
                app_payload_len = sizeof(app_payload);
                int result = rel_process_incoming(raw_receive, byte_received, &from_joiner, app_payload, &app_payload_len);
                
                if (result == 1) { // App data received
                    app_payload[app_payload_len] = '\0';
                    clean_newline(app_payload);

                    vprint("\n[VERBOSE] Received app message from %s:%d (len=%d):\n%s\n",
                           inet_ntoa(from_joiner.sin_addr),
                           ntohs(from_joiner.sin_port),
                           app_payload_len,
                           app_payload);

                    char *msg = get_message_type(app_payload);
                    if (!msg) continue;
                    
                    if (!strncmp(msg, "HANDSHAKE_REQUEST", strlen("HANDSHAKE_REQUEST"))) {
                        printf("[HOST] HANDSHAKE_REQUEST received from %s:%d\n",
                               inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port));
                        
                        // send handshake response reliably
                        seed = 12345;
                        is_handshake_done = true;
                        sprintf(full_message, "message_type: HANDSHAKE_RESPONSE\nseed: %d\n", seed);
                        
                        if (rel_send(&server_address, full_message, (int)strlen(full_message)) != 0) {
                            printf("[HOST] rel_send(HANDSHAKE_RESPONSE) failed.\n");
                        } else {
                            printf("[HOST] HANDSHAKE_RESPONSE sent to %s:%d (seed=%d). Handshake complete! (Reliable send)\n",
                                   inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port), seed);
                            vprint("\n[VERBOSE] Sent message (Reliable):\n%s\n", full_message);
                        }
                    } else if (!strncmp(msg, "BATTLE_SETUP", strlen("BATTLE_SETUP")) && is_handshake_done) {
                        printf("[HOST] BATTLE_SETUP received from joiner:\n%s\n", app_payload);
                        
                        processBattleSetup(app_payload, &joiner_setup);
                        is_battle_started = true;
                    } else if (!strncmp(msg, "CHAT_MESSAGE", strlen("CHAT_MESSAGE"))) {
                        processChatMessage(app_payload);
                        continue;
                    } else if (!strcmp(msg, "VERBOSE_ON")) {
                        VERBOSE_MODE = true;
                        printf("\n[SYSTEM] Verbose mode enabled.\n");
                    }
                    else if (!strcmp(msg, "VERBOSE_OFF")) {
                        VERBOSE_MODE = false;
                        printf("\n[SYSTEM] Verbose mode disabled.\n");
                    } else {
                        printf("[HOST] Received message from Joiner: %s\n", app_payload);
                    }
                } else if (result == 0) {
                    // ACK or non-app-data packet handled by reliability layer
                    // Do nothing here, reliability layer printed any useful info
                } else {
                    printf("[HOST] Error processing incoming packet via reliability layer.\n");
                }
            }
        }

        // If handshake done but battle not yet started, allow host to send BATTLE_SETUP
        if (is_handshake_done && !is_battle_started) {
            printf("\nType BATTLE_SETUP to send setup to joiner (or 'quit' to exit):\nmessage_type: ");
            if (fgets(buffer, MaxBufferSize, stdin) != NULL) {
                clean_newline(buffer);
                if (strcmp(buffer, "BATTLE_SETUP") == 0) {
                    // get fields
                    printf("communication_mode (P2P/BROADCAST): ");
                    if (fgets(setup.communicationMode, sizeof(setup.communicationMode), stdin) == NULL) continue;
                    clean_newline(setup.communicationMode);

                    printf("pokemon_name: ");
                    if (fgets(setup.pokemonName, sizeof(setup.pokemonName), stdin) == NULL) continue;
                    clean_newline(setup.pokemonName);

                    char stats_boosts[MaxBufferSize];
                    printf("stat_boosts (e.g., {\"special_attack_uses\": 3, \"special_defense_uses\": 2}): ");
                    if (fgets(stats_boosts, MaxBufferSize, stdin) == NULL) continue;
                    clean_newline(stats_boosts);

                    char *atk = strstr(stats_boosts, "\"special_attack_uses\": ");
                    char *def = strstr(stats_boosts, "\"special_defense_uses\": ");
                    if (!atk || !def) {
                        printf("Invalid stat format. Use keys: \"special_attack_uses\" and \"special_defense_uses\"\n");
                        continue;
                    }
                    sscanf(atk, "\"special_attack_uses\": %d", &setup.boosts.specialAttack);
                    sscanf(def, "\"special_defense_uses\": %d", &setup.boosts.specialDefense);

                    // Build and send BATTLE_SETUP reliably to known joiner address
                    sprintf(full_message,
                            "message_type: BATTLE_SETUP\n"
                            "communication_mode: %s\n"
                            "pokemon_name: %s\n"
                            "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                            setup.communicationMode, setup.pokemonName,
                            setup.boosts.specialAttack, setup.boosts.specialDefense);

                    if (rel_send(&server_address, full_message, (int)strlen(full_message)) != 0) {
                        printf("[HOST] rel_send(BATTLE_SETUP) failed.\n");
                    } else {
                        printf("[HOST] BATTLE_SETUP sent to %s:%d (Reliable send)\n", 
                               inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port));
                        is_battle_started = true;
                    }
                } else if (strcmp(buffer, "quit") == 0) {
                    break;
                } else if (strcmp(buffer, "CHAT_MESSAGE") == 0) { // Changed to == 0
                    char sender[64] = "Player 1";
                    char content_type[16];

                    printf("sender_name: Player 1\n");

                    printf("Enter content_type (TEXT/STICKER): ");
                    if (!fgets(content_type, sizeof(content_type), stdin)) continue;
                    clean_newline(content_type);

                    if (strcmp(content_type, "TEXT") == 0) {
                        char message_text[512];
                        printf("Enter message_text: ");
                        if (!fgets(message_text, sizeof(message_text), stdin)) continue;
                        clean_newline(message_text);

                        sprintf(full_message,
                            "message_type: CHAT_MESSAGE\n"
                            "sender_name: %s\n"
                            "content_type: TEXT\n"
                            "message_text: %s\n",
                            sender, message_text); // Sequence number is handled by reliability layer

                        if (rel_send(&server_address, full_message, (int)strlen(full_message)) != 0) {
                            printf("[HOST] rel_send(CHAT_MESSAGE TEXT) failed.\n");
                        } else {
                            printf("[HOST] Sent TEXT message (Reliable send).\n");
                            vprint("\n[VERBOSE] Sent message (Reliable):\n%s\n", full_message);
                        }
                    } else if (strcmp(content_type, "STICKER") == 0) {
                        char path[256];
                        printf("Path to PNG (320x320): ");
                        if (!fgets(path, sizeof(path), stdin)) continue;
                        clean_newline(path);

                        FILE *fp = fopen(path, "rb");
                        if (!fp) { printf("[ERROR] Cannot open file.\n"); continue; }

                        fseek(fp, 0, SEEK_END);
                        long fsize = ftell(fp);
                        fseek(fp, 0, SEEK_SET);

                        unsigned char *raw = malloc(fsize);
                        if (!raw) { 
                            fclose(fp); 
                            continue; }
                        fread(raw, 1, fsize, fp);
                        fclose(fp);

                        char *b64 = base64_encode(raw, fsize);
                        free(raw);
                        
                        size_t needed = strlen(b64) + 512; // 512 bytes for headers and extra fields
                        char *full_message_dyn = malloc(needed); 
                        
                        if (!full_message_dyn) { 
                            printf("[ERROR] Failed to allocate memory for message.\n"); 
                            free(b64); 
                            continue; }

                        sprintf(full_message_dyn,
                            "message_type: CHAT_MESSAGE\n"
                            "sender_name: %s\n"
                            "content_type: STICKER\n"
                            "sticker_data: %s\n",
                            sender, b64); // Sequence number is handled by reliability layer

                        if (rel_send(&server_address, full_message_dyn, (int)strlen(full_message_dyn)) != 0) {
                            printf("[HOST] rel_send(CHAT_MESSAGE STICKER) failed.\n");
                        } else {
                            printf("[HOST] Sent STICKER message (Reliable send).\n");
                            vprint("\n[VERBOSE] Sent message (Reliable):\n%s\n", full_message_dyn);
                        }
                        
                        free(b64);
                        free(full_message_dyn);
                    } else {
                        printf("[ERROR] Invalid content_type. Must be TEXT or STICKER.\n");
                    }
                } else if (!strcmp(buffer, "VERBOSE_ON")) {
                    VERBOSE_MODE = true;
                    printf("\n[SYSTEM] Verbose mode enabled.\n");

                    // Send to joiner reliably
                    sprintf(full_message, "message_type: VERBOSE_ON\n");
                    if (rel_send(&server_address, full_message, (int)strlen(full_message)) != 0) {
                        printf("[HOST] rel_send(VERBOSE_ON) failed.\n");
                    } else {
                        vprint("\n[VERBOSE] Sent verbose ON message to joiner (Reliable send)\n%s\n", full_message);
                    }
                }
                else if (!strcmp(buffer, "VERBOSE_OFF")) {
                    VERBOSE_MODE = false;
                    printf("\n[SYSTEM] Verbose mode disabled.\n");

                    // Send to joiner reliably
                    sprintf(full_message, "message_type: VERBOSE_OFF\n");
                    if (rel_send(&server_address, full_message, (int)strlen(full_message)) != 0) {
                        printf("[HOST] rel_send(VERBOSE_OFF) failed.\n");
                    } else {
                        vprint("\n[VERBOSE] Sent verbose OFF message to joiner (Reliable send)\n%s\n", full_message);
                    }
                } else {
                    printf("Invalid command. Please type BATTLE_SETUP or quit.\n");
                }
            }
        }
    }
    
    // Shutdown reliability layer before closing socket
    rel_shutdown();

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
