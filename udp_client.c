#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <conio.h>
#include <windows.h>
#include <stdint.h>

#include "game_logic.h"
#include "pokemon_data.h"
#include "BattleManager.h"

#pragma comment(lib,"ws2_32.lib")

#define MaxBufferSize 1024
#define RESEND_TIMEOUT_MS 500
#define MAX_RETRIES 3

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32]; //P2P or BROADCAST
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

bool VERBOSE_MODE = false;

void vprint(const char *fmt, ...) {
    if (!VERBOSE_MODE) return;

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// base64 helpers, for STICKERS
char* base64_encode(const unsigned char* data, size_t input_length, size_t *out_len);
unsigned char* base64_decode(const char* data, size_t input_length, size_t* out_len);

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64_encode(const unsigned char *data, size_t input_length, size_t *out_len) {
    size_t olen = 4 * ((input_length + 2) / 3);
    char *encoded = malloc(olen + 1);
    *out_len = olen;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded[j++] = base64_table[triple & 0x3F];
    }

    int mod = input_length % 3;
    if (mod) encoded[olen - 1] = '=';
    if (mod == 1) encoded[olen - 2] = '=';

    encoded[olen] = '\0';
    return encoded;
}

unsigned char* base64_decode(const char *data, size_t input_len, size_t *out_len) {
    if (input_len % 4 != 0) return NULL;

    size_t olen = input_len / 4 * 3;
    if (data[input_len - 1] == '=') olen--;
    if (data[input_len - 2] == '=') olen--;

    unsigned char *decoded = malloc(olen);
    *out_len = olen;

    int table[256];
    memset(table, -1, sizeof(table));  
    for (int i = 0; i < 64; i++) 
        table[(unsigned char) base64_table[i]] = i;

    for (size_t i = 0, j = 0; i < input_len;) {
        uint32_t sextet_a = data[i] == '=' ? 0 : table[(unsigned char)data[i]]; i++;
        uint32_t sextet_b = data[i] == '=' ? 0 : table[(unsigned char)data[i]]; i++;
        uint32_t sextet_c = data[i] == '=' ? 0 : table[(unsigned char)data[i]]; i++;
        uint32_t sextet_d = data[i] == '=' ? 0 : table[(unsigned char)data[i]]; i++;

        uint32_t triple =
            (sextet_a << 18) |
            (sextet_b << 12) |
            (sextet_c << 6) |
            sextet_d;

        if (j < olen) decoded[j++] = (triple >> 16) & 0xFF;
        if (j < olen) decoded[j++] = (triple >> 8) & 0xFF;
        if (j < olen) decoded[j++] = triple & 0xFF;
    }
    return decoded;
}

void save_sticker(const char *filename, unsigned char *data, size_t len) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    fwrite(data, 1, len, f);
    fclose(f);
}

char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
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

// returns true if message contains a reply: header (any value)
bool message_has_reply_tag(const char *payload) {
    return strstr(payload, "reply:") != NULL;
}

// central sequenced send: ensures reply tag exists (unless already present),
// appends seq:, and puts message into the single outstanding pending slot
int send_sequenced_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    if (pending.occupied) {
        printf("[CLIENT] Cannot send: another message is waiting for ACK (seq=%d)\n", pending.seq);
        return -1;
    }

    // Build payload with reply tag if missing
    char tagged_payload[MaxBufferSize * 2];
    if (message_has_reply_tag(payload)) {
        strncpy(tagged_payload, payload, sizeof(tagged_payload) - 1);
        tagged_payload[sizeof(tagged_payload)-1] = '\0';
    } else {
        snprintf(tagged_payload, sizeof(tagged_payload), "reply: 1\n%s", payload);
    }

    int myseq = next_seq++;
    char composed[MaxBufferSize * 2];
    snprintf(composed, sizeof(composed), "%s\nseq: %d", tagged_payload, myseq);

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

// detect reply-markers to avoid reply-bounce loops (incoming)
bool message_is_reply(const char *message) {
    return strstr(message, "reply: 1") != NULL || strstr(message, "reply:") != NULL;
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
    //char chosenPokemon[64];
    init_battle(&ctx, 0, "Charmander");

    BattleManager bm;
    BattleManager_Init(&bm, 0, "Charmander");

    GameState lastState = bm.ctx.currentState;
    int lastTurn = bm.ctx.isMyTurn;
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
        // Now all outgoing sequenced messages are auto-tagged by send_sequenced_message
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

                // If ACK - clear pending if matches
                if (!strncmp(msg, "ACK", 3)) {
                    int ack_seq = get_seq_from_message(receive);
                    if (ack_seq >= 0 && pending.occupied && ack_seq == pending.seq) {
                        printf("\n[RECEIVED ACK seq=%d] Clearing pending.\n", ack_seq);
                        pending.occupied = false;
                        if (!isSpectator) {
                        printf("message_type: ");
                        fflush(stdout);
        }
    }
                    continue;
                }

                // If incoming message contains seq:, always send an ACK
                int incoming_seq = get_seq_from_message(receive);
                if (incoming_seq >= 0) {
                    send_ack(socket_network, incoming_seq, &from_server, from_len);
                }

                BattleManager_HandleIncoming(&bm, receive);
                const char* outgoing = BattleManager_GetOutgoingMessage(&bm);
                if (strlen(outgoing) > 0) {
                    send_sequenced_message(socket_network, outgoing, &server_address, sizeof(server_address));
                    BattleManager_ClearOutgoingMessage(&bm);
                }
//  Discrepancy Resolution
if (strcmp(msg, "CALCULATION_REPORT") == 0) {

    char peerMove[64];
    int peerDamage, peerRemainingHP;
    char attacker[64];

    extract_value(receive, "move_used", peerMove);
    extract_value(receive, "damage_dealt", buffer); peerDamage = atoi(buffer);
    extract_value(receive, "remaining_health", buffer); peerRemainingHP = atoi(buffer);
    extract_value(receive, "attacker", attacker);

    bool match = (strcmp(peerMove, bm.ctx.lastMoveUsed) == 0) &&
                 (peerDamage == bm.ctx.lastDamage) &&
                 (peerRemainingHP == bm.ctx.lastRemainingHP);

    if (match) {
        snprintf(full_message, sizeof(full_message),
            "message_type: CALCULATION_CONFIRM\nsequence_number: %d",
            ++bm.ctx.currentSequenceNum);
        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));

        bm.ctx.isMyTurn = !bm.ctx.isMyTurn;
        bm.ctx.currentState = STATE_WAITING_FOR_MOVE;
    } else {
        snprintf(full_message, sizeof(full_message),
            "message_type: RESOLUTION_REQUEST\n"
            "move_used: %s\n"
            "damage_dealt: %d\n"
            "remaining_health: %d\n"
            "sequence_number: %d",
            bm.ctx.lastMoveUsed,
            bm.ctx.lastDamage,
            bm.ctx.lastRemainingHP,
            ++bm.ctx.currentSequenceNum);
        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));

        bm.ctx.currentState = STATE_WAITING_FOR_RESOLUTION;
    }
}

else if (strcmp(msg, "RESOLUTION_REQUEST") == 0) {

    char reqMove[64];
    int reqDamage, reqRemainingHP;
    extract_value(receive, "move_used", reqMove);
    extract_value(receive, "damage_dealt", buffer); reqDamage = atoi(buffer);
    extract_value(receive, "remaining_health", buffer); reqRemainingHP = atoi(buffer);

    bool agree = (strcmp(reqMove, bm.ctx.lastMoveUsed) == 0) &&
                 (reqDamage == bm.ctx.lastDamage) &&
                 (reqRemainingHP == bm.ctx.lastRemainingHP);

    if (agree) {
        snprintf(full_message, sizeof(full_message),
            "message_type: ACK\nsequence_number: %d",
            ++bm.ctx.currentSequenceNum);
        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));

        bm.ctx.currentState = STATE_WAITING_FOR_MOVE;
        bm.ctx.isMyTurn = !bm.ctx.isMyTurn;
    } else {
        printf("[ERROR] Discrepancy could not be resolved. Terminating battle.\n");
        bm.ctx.currentState = STATE_GAME_OVER;
    }
}

            if (!strncmp(msg, "CHAT_MESSAGE", 12)) {
                    char *text = strstr(receive, "text: ");
                    if (text) text += 6;
                    printf("\n[CHAT] %s\n", text);
                    continue;
                }

                if (!strncmp(msg, "STICKER_MESSAGE", 15)) {
                    char *data = strstr(receive, "data: ");
                    if (data) data += 6;

                    printf("\n[STICKER RECEIVED → saved as output_sticker.png]\n");

                    size_t outlen;
                    unsigned char *decoded = base64_decode(data, strlen(data), &outlen);
                    save_sticker("output_sticker.png", decoded, outlen);
                    free(decoded);

                    continue;
                }

                if (!strncmp(msg, "VERBOSE_TOGGLE", 14)) {
                    VERBOSE_MODE = !VERBOSE_MODE;
                    printf("[VERBOSE] now %s\n", VERBOSE_MODE ? "ON" : "OFF");
                    continue;
                }

                // Application-level prints
                if (!strncmp(msg, "HANDSHAKE_RESPONSE", 18)) {
                    char *seed_ptr = strstr(receive, "seed:");
                    if (seed_ptr) sscanf(seed_ptr, "seed: %d", &seed);
                    printf("\n[SERVER] Handshake OK (seed=%d)\n", seed);
                    printf("message_type: ");
                }
                else if (!strncmp(msg, "SPECTATOR_RESPONSE", 18)) {
                    printf("\n[SERVER] You are now a Spectator.\n");
                    isSpectator = true;
                }
                else {
                    printf("\n[UPDATE RECEIVED MESSAGE]\n%s\n", receive);
                    printf("message_type: ");
                    }
            
            }
        }
        // update prompt state
            if (bm.ctx.currentState != lastState || bm.ctx.isMyTurn != lastTurn) {
            promptPrinted = false;
            lastState = bm.ctx.currentState;
            lastTurn = bm.ctx.isMyTurn;
        }

        // Retransmission logic
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

        // User input (only when no pending outstanding message)
        if (!pending.occupied) {
            if (isSpectator) {
                if (_kbhit()) {
                    printf("\nSpectator (Chat only): ");
                    if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                    clean_newline(buffer);
                    if (strlen(buffer) == 0) continue;
                    sprintf(full_message, "message_type: CHAT_MESSAGE\nsender_name: Spec\nmessage_text: %s", buffer);
                    // send_sequenced_message will tag as reply:1 automatically
                    send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                }
            }
            else {
                if (bm.ctx.currentState == STATE_WAITING_FOR_MOVE && bm.ctx.isMyTurn) {
                if (!promptPrinted) {
        printf("\n[YOUR TURN] Enter Move Name: ");
        promptPrinted = true;
    }
}

            if (_kbhit()) {
                if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                clean_newline(buffer);
                if (strlen(buffer) == 0) continue;

                if (strncmp(buffer, "/chat ", 6) == 0) {
                    char buf[1024];
                    sprintf(buf,
                        "message_type: CHAT_MESSAGE\ntext: %s",
                        buffer + 6);

                    send_sequenced_message(socket_network, buf, &server_address, sizeof(server_address));
                    continue;
                }

                if (strncmp(buffer, "/sticker ", 9) == 0) {
                    char *filename = buffer + 9;
                    filename[strcspn(filename, "\n")] = 0;

                    FILE *f = fopen(filename, "rb");
                    if (!f) {
                        printf("File not found.\n");
                        continue;
                    }

                    fseek(f, 0, SEEK_END);
                    long size = ftell(f);
                    rewind(f);

                    unsigned char *raw = malloc(size);
                    fread(raw, 1, size, f);
                    fclose(f);

                    size_t outlen;
                    char *encoded = base64_encode(raw, size, &outlen);

                    char buf[5000];
                    sprintf(buf,
                        "message_type: STICKER_MESSAGE\ndata: %s",
                        encoded);

                    send_sequenced_message(socket_network, buf, &server_address, sizeof(server_address));

                    free(raw);
                    free(encoded);
                    continue;
                }

                if (strncmp(buffer, "/verbose", 8) == 0) {
                    send_sequenced_message(socket_network,
                        "message_type: VERBOSE_TOGGLE",
                        &server_address, sizeof(server_address));
                    continue;
                }

                if (strcmp(buffer, "ATTACK_ANNOUNCE") == 0) {
                    char moveName[64];
                    printf("Enter Move Name: ");
                    if (fgets(moveName, sizeof(moveName), stdin) == NULL) continue;
                    clean_newline(moveName);

                    snprintf(full_message, sizeof(full_message),
                        "message_type: \"ATTACK_ANNOUNCE\"\n"
                        "move_name: %s\n"
                        "sequence_number: %d",
                        moveName,
                        ++bm.ctx.currentSequenceNum);

                    send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                    continue;
                }

            if (strcmp(buffer, "DEFENSE_ANNOUNCE") == 0) {
                snprintf(full_message, sizeof(full_message),
                    "message_type: \"DEFENSE_ANNOUNCE\"\n"
                    "sequence_number: %d",
                    ++bm.ctx.currentSequenceNum);

                send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                continue;
            }
// CALCULATION_REPORT
bool triggerCalcReport = false;

if (bm.ctx.currentState == STATE_WAITING_FOR_MOVE && bm.ctx.isMyTurn && strlen(bm.ctx.lastMoveUsed) > 0) {
    triggerCalcReport = true;
}

if (strcmp(buffer, "CALCULATION_REPORT") == 0) {
    if (strlen(bm.ctx.lastMoveUsed) > 0) {
        triggerCalcReport = true;
    } else {
        printf("No move recorded yet to send CALCULATION_REPORT.\n");
    }
}

if (triggerCalcReport) {
    char* moveUsed = bm.ctx.lastMoveUsed;

    int damageDealt = bm.ctx.oppPokemon.prevHp - bm.ctx.oppPokemon.hp;
    if (damageDealt < 0) damageDealt = 0;

    int remainingHP = bm.ctx.oppPokemon.hp;

    snprintf(full_message, sizeof(full_message),
        "message_type: CALCULATION_REPORT\n"
        "attacker: %s\n"
        "move_used: %s\n"
        "remaining_health: %d\n"
        "damage_dealt: %d\n"
        "defender_hp_remaining: %d\n"
        "status_message: %s used %s! It was super effective!\n"
        "sequence_number: %d",
        bm.ctx.myPokemon.name,
        moveUsed,
        bm.ctx.myPokemon.hp,
        damageDealt,
        remainingHP,
        bm.ctx.myPokemon.name,
        moveUsed,
        ++bm.ctx.currentSequenceNum);

    send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));

    bm.ctx.lastMoveUsed[0] = '\0';
}


            if (strcmp(buffer, "CALCULATION_CONFIRM") == 0) {
                snprintf(full_message, sizeof(full_message),
                    "message_type: \"CALCULATION_CONFIRM\"\n"
                    "sequence_number: %d",
                    ++bm.ctx.currentSequenceNum);

                send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                continue;
            }

                if (bm.ctx.currentState == STATE_WAITING_FOR_MOVE && bm.ctx.isMyTurn) {
                BattleManager_HandleUserInput(&bm, buffer);

                const char* outgoing = BattleManager_GetOutgoingMessage(&bm);
                if (strlen(outgoing) > 0) {
                send_sequenced_message(socket_network, outgoing, &server_address, sizeof(server_address));
                BattleManager_ClearOutgoingMessage(&bm);
        }
        promptPrinted = false;
        continue;
    }
if (strcmp(buffer, "GAME_OVER") == 0) {
    snprintf(full_message, sizeof(full_message),
        "message_type: GAME_OVER\n"
        "winner: %s\n"
        "loser: %s\n"
        "sequence_number: %d",
        bm.ctx.myPokemon.name,      // the winner
        bm.ctx.oppPokemon.name,     // the loser
        ++bm.ctx.currentSequenceNum
    );

    send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
    
    // Trigger local game over logic
    BattleManager_TriggerGameOver(&bm, bm.ctx.myPokemon.name, bm.ctx.oppPokemon.name);
    
    promptPrinted = false;
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
                            // gets the integer from the string
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
                            // user-initiated -> send (auto-tagged in send_sequenced_message)
                            send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                            break;
                        }
                    }
                    else {
                        sprintf(full_message, "message_type: %s", buffer);
                        // user-initiated -> send (auto-tagged)
                        send_sequenced_message(socket_network, full_message, &server_address, sizeof(server_address));
                        
                        int result = BattleManager_CheckWinLoss(&bm);
                        if (result == 1) {
                        printf("You won!\n");
                        break;
                        } else if (result == -1) {
                        printf("You lost!\n");
                        break;
                        }
                    }
                }
            }
        }
    }

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
