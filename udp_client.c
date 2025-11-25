#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <conio.h>
#include <windows.h>

#include "game_logic.h"
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
    char communicationMode[32];  //P2P or BROADCAST
    char pokemonName[64];
    StatBoosts boosts;
} BattleSetupData;

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

static int next_seq = 1;
static PendingMsg pending;

void init_pending() {
    pending.occupied = false;
    pending.len = 0;
    pending.seq = 0;
    pending.retries = 0;
    pending.last_sent_ms = 0;
    pending.dest_len = 0;
}

char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
    return NULL;
}

void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) str[len - 1] = '\0';
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

void build_ack_message(char *out, int seq) {
    sprintf(out, "message_type: ACK\nseq: %d", seq);
}

int raw_sendto(SOCKET s, const char *buf, int len, const struct sockaddr_in *to, int tolen) {
    return sendto(s, buf, len, 0, (SOCKADDR*)to, tolen);
}

int send_sequenced_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    if (pending.occupied) {
        printf("[CLIENT] Cannot send: another message is waiting for ACK (seq=%d)\n", pending.seq);
        return -1;
    }
    int myseq = next_seq++;
    char composed[MaxBufferSize * 2];
    snprintf(composed, sizeof(composed), "%s\nseq: %d", payload, myseq);

    pending.occupied = true;
    pending.seq = myseq;
    pending.retries = 0;
    pending.last_sent_ms = GetTickCount();
    pending.len = (int)strlen(composed);
    if (pending.len >= (int)sizeof(pending.data)) pending.len = (int)sizeof(pending.data) - 1;
    memcpy(pending.data, composed, pending.len);
    pending.data[pending.len] = '\0';
    pending.dest = *dest;
    pending.dest_len = dest_len;

    int sent = raw_sendto(sock, pending.data, pending.len, &pending.dest, pending.dest_len);
    if (sent == SOCKET_ERROR) {
        printf("[CLIENT] sendto() error: %d\n", WSAGetLastError());
    } else {
        printf("\n[SENT seq=%d] %s\n", pending.seq, pending.data);
    }
    return 0;
}

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


    BattleContext ctx;
    char responseBuf[MaxBufferSize];
    init_battle(&ctx, 0, "JoinerPokemon"); 
    
    GameState lastState = ctx.currentState;
    int lastTurn = ctx.isMyTurn;
    bool promptPrinted = false;


    printf("Client starting...\n");

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

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    init_pending();
    loadPokemonCSV("pokemon.csv");

    printf("Type HANDSHAKE_REQUEST or SPECTATOR_REQUEST\n");
    printf("message_type: ");
    
    if (fgets(buffer, MaxBufferSize, stdin) != NULL) {
        clean_newline(buffer);
        sprintf(full_message, "message_type: %s", buffer);
        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
    }

    while (1) {
        fd_set readfds;
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(socket_network, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;

        int activity = select(0, &readfds, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR) {
            printf("select() error\n");
            break;
        }

        if (FD_ISSET(socket_network, &readfds)) {
            memset(receive, 0, sizeof(receive));
            int byte_received = recvfrom(socket_network, receive, sizeof(receive) - 1, 0, (SOCKADDR*)&from_server, &from_len);

            if (byte_received > 0) {
                receive[byte_received] = '\0';
                char *msg = get_message_type(receive);
                if (!msg) continue;

                if (!strncmp(msg, "ACK", 3)) {
                    int ack_seq = get_seq_from_message(receive);
                    if (ack_seq >= 0 && pending.occupied && ack_seq == pending.seq) {
                        printf("\n[RECEIVED ACK seq=%d] Clearing pending.\n", ack_seq);
                        pending.occupied = false;
                    }
                    continue;
                }

                int incoming_seq = get_seq_from_message(receive);
                if (incoming_seq >= 0) {
                    send_ack(socket_network, incoming_seq, &from_server, from_len);
                }

                process_incoming_packet(&ctx, receive, responseBuf);
                if (strlen(responseBuf) > 0) {
                    send_sequenced_message(socket_network, responseBuf, &server_address, sizeof(server_address));
                }
            

                if (!strncmp(msg, "HANDSHAKE_RESPONSE", 18)) {
                    char *seed_ptr = strstr(receive, "seed:");
                    if (seed_ptr) sscanf(seed_ptr, "seed: %d", &seed);
                    printf("\n[SERVER] Handshake OK (seed=%d)\n", seed);
                    printf("You may now type 'BATTLE_SETUP' to begin configuration.\n");
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
        
        if (ctx.currentState != lastState || ctx.isMyTurn != lastTurn) {
            promptPrinted = false;
            lastState = ctx.currentState;
            lastTurn = ctx.isMyTurn;
        }

        if (pending.occupied) {
            DWORD now = GetTickCount();
            if ((now - pending.last_sent_ms) >= RESEND_TIMEOUT_MS) {
                if (pending.retries >= MAX_RETRIES) {
                    printf("\n[CLIENT] Message seq=%d failed max retries.\n", pending.seq);
                    closesocket(socket_network);
                    WSACleanup();
                    return 0;
                } else {
                    int sent = raw_sendto(socket_network, pending.data, pending.len, &pending.dest, pending.dest_len);
                    pending.retries++;
                    pending.last_sent_ms = now;
                    printf("[CLIENT] Retransmit seq=%d (retry %d)\n", pending.seq, pending.retries);
                }
            }
        }

        if (!pending.occupied) {
            if (isSpectator) {
                if (_kbhit()) {
                    printf("\nSpectator (Chat only): ");
                    if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                    clean_newline(buffer);
                    if (strlen(buffer) == 0) continue;
                    sprintf(full_message, "message_type: CHAT_MESSAGE\nsender_name: Spec\nmessage_text: %s", buffer);
                    send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                }
            }
            else {
                if (ctx.currentState == STATE_WAITING_FOR_MOVE && ctx.isMyTurn) {
                    if (!promptPrinted) {
                        printf("\n[YOUR TURN] Enter Move Name: ");
                        promptPrinted = true;
                    }
                }

                if (_kbhit()) {
                    if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                    clean_newline(buffer);
                    if (strlen(buffer) == 0) continue;

                    if (ctx.currentState == STATE_WAITING_FOR_MOVE && ctx.isMyTurn) {
                        process_user_input(&ctx, buffer, responseBuf);
                        if (strlen(responseBuf) > 0) {
                            send_sequenced_message(socket_network, responseBuf, &server_address, sizeof(server_address));
                            promptPrinted = false; 
                        }
                        continue; 
                    }

                    if (strcmp(buffer, "BATTLE_SETUP") == 0) {
                        while (1) {
                            printf("communication_mode (P2P/BROADCAST): ");
                            if (fgets(setup.communicationMode, sizeof(setup.communicationMode), stdin) == NULL) continue;
                            clean_newline(setup.communicationMode);
                            
                            printf("pokemon_name: ");
                            if (fgets(setup.pokemonName, sizeof(setup.pokemonName), stdin) == NULL) continue;
                            clean_newline(setup.pokemonName);

                            sprintf(full_message,
                                "message_type: BATTLE_SETUP\n"
                                "communication_mode: %s\n"
                                "pokemon_name: %s\n"
                                "stat_boosts: { \"special_attack_uses\": 5, \"special_defense_uses\": 5 }\n",
                                setup.communicationMode,
                                setup.pokemonName
                            );
                            send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                            break;
                        }
                    }
                    else {
                        sprintf(full_message, "message_type: %s", buffer);
                        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                    }
                }
            }
        }
    }
    closesocket(socket_network);
    WSACleanup();
    return 0;
}