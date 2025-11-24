#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <conio.h>
#include <windows.h>

//file loads Pokemon data
#include "pokemon_data.h"

#pragma comment(lib,"ws2_32.lib")

#define MaxBufferSize 1024
#define RESEND_TIMEOUT_MS 500
#define MAX_RETRIES 3

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32]; // P2P or BROADCAST
    char pokemonName[64];
    StatBoosts boosts;
} BattleSetupData;

// outgoing-pending message structure (one slot)
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

static int next_seq = 1; // next sequence number to use for outgoing messages
static PendingMsg pending;

void init_pending() {
    pending.occupied = false;
    pending.len = 0;
    pending.seq = 0;
    pending.retries = 0;
    pending.last_sent_ms = 0;
    pending.dest_len = 0;
}

// Extract message after "message_type: "
char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
    return NULL;
}

// Remove trailing newline
void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && (str[len - 1] == '\n' || str[len-1] == '\r')) str[len - 1] = '\0';
    // handle \r\n case
    len = strlen(str);
    if (len > 0 && str[len - 1] == '\r') str[len - 1] = '\0';
}

int get_seq_from_message(const char *message) {
    const char *p = strstr(message, "seq:");
    if (!p) return -1;
    int s = -1;
    if (sscanf(p, "seq: %d", &s) == 1) return s;
    return -1;
}

bool message_has_seq(const char *message) {
    return strstr(message, "seq:") != NULL;
}

// Build ACK message text for seq
void build_ack_message(char *out, int seq) {
    sprintf(out, "message_type: ACK\nseq: %d", seq);
}

// send raw bytes (no seq appended)
// used internally to send retransmits / ACKs
int raw_sendto(SOCKET s, const char *buf, int len, const struct sockaddr_in *to, int tolen) {
    return sendto(s, buf, len, 0, (SOCKADDR*)to, tolen);
}

// Send a sequenced message: append seq line and put into pending slot
// Returns 0 on success starting the send (message buffered and sent), -1 if pending occupied
int send_sequenced_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    if (pending.occupied) {
        // we only support one outstanding message at a time in this simple implementation
        printf("[CLIENT] Cannot send: another message is waiting for ACK (seq=%d)\n", pending.seq);
        return -1;
    }

    int myseq = next_seq++;
    char composed[MaxBufferSize * 2];
    // append seq as separate line
    snprintf(composed, sizeof(composed), "%s\nseq: %d", payload, myseq);

    // fill pending
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
        printf("[CLIENT] sendto() error: %d\n", WSAGetLastError());
        // we still consider message pending; retransmit logic will attempt again
    } else {
        printf("\n[SENT seq=%d] %s\n", pending.seq, pending.data);
    }
    return 0;
}

// Send a non-sequenced (normal) message immediately (no reliability)
int send_plain_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    int sent = raw_sendto(sock, payload, (int)strlen(payload), dest, dest_len);
    if (sent == SOCKET_ERROR) {
        printf("[CLIENT] sendto() error: %d\n", WSAGetLastError());
        return -1;
    }
    return 0;
}

// Send ACK to a specific address/port
void send_ack(SOCKET sock, int seq, const struct sockaddr_in *to, int tolen) {
    char ack[128];
    build_ack_message(ack, seq);
    int sent = raw_sendto(sock, ack, (int)strlen(ack), to, tolen);
    if (sent == SOCKET_ERROR) {
        printf("[CLIENT] Failed to send ACK for seq %d: %d\n", seq, WSAGetLastError());
    } else {
        printf("[SENT ACK seq=%d]\n", seq);
    }
}

int main() {
    WSADATA wsa;
    SOCKET socket_network;
    struct sockaddr_in server_address, from_server;
    int from_len = sizeof(from_server);
    BattleSetupData setup;
    char receive[MaxBufferSize * 2];
    char buffer[MaxBufferSize];
    char full_message[MaxBufferSize * 2];
    bool isSpectator = false;
    int seed = 0;

    printf("Client starting...\n");

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // Create UDP socket
    socket_network = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_network == INVALID_SOCKET) {
        printf("socket() failed.\n");
        WSACleanup();
        return 1;
    }

    // Setup server address
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    init_pending();

    loadPokemonCSV("pokemon.csv"); //load data into program

    printf("Type HANDSHAKE_REQUEST or SPECTATOR_REQUEST\n");
    printf("To chat in spectator mode, press any key in your keyboard\n");
    printf("message_type: ");
    // Send initial request (we will send reliably)
    if (fgets(buffer, MaxBufferSize, stdin) != NULL) {
        clean_newline(buffer);
        sprintf(full_message,"message_type: %s", buffer);
        // For these initial requests we will send as sequenced messages so the server can ACK them
        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
    }

    while (1) {
        /* -----------------------------
           1. Check for incoming messages
        ------------------------------ */
        fd_set readfds;
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(socket_network, &readfds);
        // small select interval to allow retransmit checks frequently
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10 ms

        int activity = select(0, &readfds, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR) {
            printf("select() error\n");
            break;
        }

        if (FD_ISSET(socket_network, &readfds)) {
            memset(receive, 0, sizeof(receive));
            int byte_received = recvfrom(socket_network, receive, sizeof(receive)-1, 0, (SOCKADDR*)&from_server, &from_len);

            if (byte_received > 0) {
                receive[byte_received] = '\0';
                char *msg = get_message_type(receive);
                if (!msg) continue;

                // If it's an ACK, parse seq and clear pending if matches
                if (!strncmp(msg, "ACK", 3)) {
                    int ack_seq = get_seq_from_message(receive);
                    if (ack_seq >= 0 && pending.occupied && ack_seq == pending.seq) {
                        printf("\n[RECEIVED ACK seq=%d] Clearing pending.\n", ack_seq);
                        pending.occupied = false;
                    } else {
                        printf("\n[RECEIVED ACK seq=%d] (no matching pending)\n", ack_seq);
                    }
                    continue;
                }

                // For other incoming messages that have seq: send ACK back
                int incoming_seq = get_seq_from_message(receive);
                if (incoming_seq >= 0) {
                    // send ACK immediately back to sender
                    send_ack(socket_network, incoming_seq, &from_server, from_len);
                }

                // Handle application-level messages
                if (!strncmp(msg, "HANDSHAKE_RESPONSE", 18)) {
                    char *seed_ptr = strstr(receive, "seed:");
                    if (seed_ptr) sscanf(seed_ptr, "seed: %d", &seed);
                    printf("\n[SERVER] Handshake OK (seed=%d)\n", seed);
                }
                else if (!strncmp(msg, "SPECTATOR_RESPONSE", 18)) {
                    printf("\n[SERVER] You are now a Spectator.\n");
                    isSpectator = true;
                }
                else {
                    printf("\n[UPDATE] %s\n", msg);
                }
            }
        }

        /* -----------------------------
           2. Retransmission logic for pending outgoing message
        ------------------------------ */
        if (pending.occupied) {
            DWORD now = GetTickCount();
            if ((now - pending.last_sent_ms) >= RESEND_TIMEOUT_MS) {
                if (pending.retries >= MAX_RETRIES) {
                    printf("\n[CLIENT] Message seq=%d failed after %d retries. Terminating connection.\n", pending.seq, pending.retries);
                    // close socket and exit
                    closesocket(socket_network);
                    WSACleanup();
                    return 0;
                } else {
                    // retransmit
                    int sent = raw_sendto(socket_network, pending.data, pending.len, &pending.dest, pending.dest_len);
                    pending.retries++;
                    pending.last_sent_ms = now;
                    if (sent == SOCKET_ERROR) {
                        printf("[CLIENT] Retransmit seq=%d failed to send: %d\n", pending.seq, WSAGetLastError());
                    } else {
                        printf("[CLIENT] Retransmit seq=%d (retry %d)\n", pending.seq, pending.retries);
                    }
                }
            }
        }

        /* -----------------------------
           3. Check for keyboard input
        ------------------------------ */
        if (isSpectator) {
            if (_kbhit()) {
                printf("\nSpectator (Chat only): ");
                if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                clean_newline(buffer);
                if (strlen(buffer) == 0) continue;

                sprintf(full_message, "message_type: CHAT_MESSAGE %s", buffer);
                // chat messages should be reliable (sequenced)
                send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
            }
        }
        else {
            // only allow user to send a new sequenced message if there's no pending outstanding one
            printf("\nmessage_type: ");
            if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
            clean_newline(buffer);
            if (strlen(buffer) == 0) continue;

            if (strcmp(buffer, "BATTLE_SETUP") == 0) {
                // --- Gather BATTLE_SETUP info ---
                while (1) {
                    printf("communication_mode (P2P/BROADCAST): ");
                    if (fgets(setup.communicationMode, sizeof(setup.communicationMode), stdin) == NULL) continue;
                    clean_newline(setup.communicationMode);
                    if (strcmp(setup.communicationMode, "P2P") != 0 && strcmp(setup.communicationMode, "BROADCAST") != 0) {
                        printf("Invalid mode. Try again.\n");
                        continue;
                    }

                    printf("pokemon_name: ");
                    if (fgets(setup.pokemonName, sizeof(setup.pokemonName), stdin) == NULL) continue;
                    clean_newline(setup.pokemonName);

                    char stats_boosts[MaxBufferSize];
                    printf("stat_boosts (e.g., {\"special_attack_uses\": 3, \"special_defenses_uses\": 2}): ");
                    if (fgets(stats_boosts, MaxBufferSize, stdin) == NULL) continue;
                    clean_newline(stats_boosts);

                    char *atk = strstr(stats_boosts, "\"special_attack_uses\": ");
                    char *def = strstr(stats_boosts, "\"special_defenses_uses\": ");
                    if (!atk || !def) {
                        printf("Invalid stat format.\n");
                        continue;
                    }

                    sscanf(atk, "\"special_attack_uses\": %d", &setup.boosts.specialAttack);
                    sscanf(def, "\"special_defenses_uses\": %d", &setup.boosts.specialDefense);

                    sprintf(full_message,
                        "message_type: BATTLE_SETUP\n"
                        "communication_mode: %s\n"
                        "pokemon_name: %s\n"
                        "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                        setup.communicationMode,
                        setup.pokemonName,
                        setup.boosts.specialAttack,
                        setup.boosts.specialDefense
                    );

                    // --- Send message --- (reliably)
                    if (strcmp(setup.communicationMode, "BROADCAST") == 0) {
                        struct sockaddr_in broadcastAddr;
                        broadcastAddr.sin_family = AF_INET;
                        broadcastAddr.sin_port = htons(9002);
                        broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

                        int broadcastEnable = 1;
                        setsockopt(socket_network, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));

                        // for broadcast, destination is broadcastAddr
                        send_sequenced_message(socket_network, full_message, &broadcastAddr, sizeof(broadcastAddr));
                        printf("\n[BROADCAST SEND REQUESTED]\n%s\n", full_message);
                    } else {
                        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                        printf("\n[P2P SEND REQUESTED]\n%s\n", full_message);
                    }
                    break; // exit BATTLE_SETUP loop
                }
            }
            else {
                sprintf(full_message, "message_type: %s", buffer);
                // Choose: if it's a chat/battle message we'll send reliably, otherwise plain.
                // For simplicity send all non-empty user messages reliably (so chat and battle messages covered).
                send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                printf("\n[SENT REQUESTED] %s\n", full_message);
            }
        }
    }

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
