#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
<<<<<<< HEAD
#include <conio.h>
=======
#include "battle_setup.h"
#include "attack_announce.h"
#include "defense_announce.h"
#include "calculation_report.h"
#include "calculation_confirm.h"
>>>>>>> origin/csnetwk1

#include "game_logic.h"
#include "pokemon_data.h"

#define MaxBufferSize 1024
#define MaxClients 10
#pragma comment(lib,"ws2_32.lib")

#define RESEND_TIMEOUT_MS 500
#define MAX_RETRIES 3
<<<<<<< HEAD

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;
=======
>>>>>>> origin/csnetwk1

typedef enum {
    TURN_IDLE,
    TURN_WAIT_DEFENSE,
    TURN_WAIT_CALC_REPORT,
    TURN_WAIT_CALC_CONFIRM
} TurnState;

TurnState currentTurnState = TURN_IDLE;
int lastSeq = 0;   // Track the sequence number for the current turn
CalculationReport localCalcReport; // store your own calculated damage


typedef struct{
    struct sockaddr_in addr;
    int addr_len;
    bool active;
    bool isSpectator;
    BattleSetupData battlesetup;
} Player;

bool spectActive = false;
SOCKADDR_IN SpectatorADDR;
int SpectatorAddrSize = sizeof(SpectatorADDR);

bool joinerActive = false;
SOCKADDR_IN JoinerADDR;
int JoinerAddrSize = sizeof(JoinerADDR);

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

// returns true if message contains a reply: header (any value)
bool message_has_reply_tag(const char *payload) {
    return strstr(payload, "reply:") != NULL;
}

// central sequenced send - tags outgoing payload with reply:1 if missing,
// appends seq:, and places into global pending slot
int send_sequenced_message(SOCKET sock, const char *payload, struct sockaddr_in *dest, int dest_len) {
    if (pending.occupied) {
        printf("[SERVER] Cannot send now: another message waiting for ACK (seq=%d)\n", pending.seq);
        return -1;
    }
    static int next_seq = 1;

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
    if (pending.len >= (int)sizeof(pending.data)) pending.len = (int)sizeof(pending.data)-1;
    memcpy(pending.data, composed, pending.len);
    pending.data[pending.len] = '\0';
    pending.dest = *dest;
    pending.dest_len = dest_len;

    int sent = raw_sendto(sock, pending.data, pending.len, &pending.dest, pending.dest_len);
    if (sent == SOCKET_ERROR) {
        printf("[SERVER] sendto() error: %d\n", WSAGetLastError());
    } else {
        printf("[SERVER] Sent sequenced (seq=%d)\n", pending.seq);
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
        // Optionally print ACK send debug
        printf("[SERVER] Sent ACK seq=%d\n", seq);
    }
}

// Helper function to find a client index based on their address
int checkClients(struct sockaddr_in SenderAddr, Player clients[MaxClients]) {
    for (int i = 0; i < MaxClients; i++) {
        if (clients[i].active && clients[i].addr.sin_addr.s_addr == SenderAddr.sin_addr.s_addr)
            return i;
    }

    // Client not found
    return -1; 
}

// forward to spectator reliably - send_sequenced_message will tag as reply if needed
void spectatorUpdateReliable(const char *updateMessage, SOCKET socket) {
    if (!spectActive) return;
    struct sockaddr_in dest = SpectatorADDR;
    int destlen = SpectatorAddrSize;

    int r = send_sequenced_message(socket, updateMessage, &dest, destlen);
    if (r != 0) {
        printf("[SERVER] Spectator update dropped (pending occupied)\n");
    }
}

// --- MAIN FUNCTION ---
int main() {
    WSADATA wsa;
    char receive[MaxBufferSize * 2];
    char response[MaxBufferSize * 2];
    char inputBuffer[MaxBufferSize];
    struct sockaddr_in SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);
    Player clients[MaxClients];

    BattleContext ctx;
    char logicResponseBuf[MaxBufferSize];

    init_battle(&ctx, 1, "HostPokemon");

    GameState lastState = ctx.currentState;
    int lastTurn = ctx.isMyTurn;
    bool promptPrinted = false;

    for (int i = 0; i < MaxClients; i++) {
        clients[i].active = false;
    }

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

    int broadcastEnable = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (SOCKADDR *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("Server: bind() failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }

    init_pending();

    printf("Server ready. Listening on port 9002...\n");
    loadPokemonCSV("pokemon.csv");

    bool running = true;

    while (running) {
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

            if (ByteReceived > 0) {
                receive[ByteReceived] = '\0';
                char *msg = get_message_type(receive);

                if (msg) {
                    // If it's an ACK, clear pending if matches
                    if (!strncmp(msg, "ACK", 3)) {
                        int ack_seq = get_seq_from_message(receive);
                        if (ack_seq >= 0 && pending.occupied && ack_seq == pending.seq) {
                            printf("[SERVER] Received ACK seq=%d: clearing pending\n", ack_seq);
                            pending.occupied = false;
                        }
                        goto retransmit_check;
                    }

                    // If incoming has seq:, always send ACK back right away
                    int incoming_seq = get_seq_from_message(receive);
                    if (incoming_seq >= 0) {
                        send_ack(server_socket, incoming_seq, &SenderAddr, SenderAddrSize);
                    }

                    // Process incoming message for game state
                    process_incoming_packet(&ctx, receive, logicResponseBuf);

                    // If logic produced a response, send it only if incoming wasn't already a reply
                    if (strlen(logicResponseBuf) > 0 && !message_is_reply(receive)) {
                        send_sequenced_message(server_socket, logicResponseBuf, &SenderAddr, SenderAddrSize);
                    }

                    // Handle Handshakes and message types
                    if (strncmp(msg, "exit", 4) == 0) {
                        printf("[SERVER] Exit command received.\n");
                        running = false;
                    }
                    else if (strncmp(msg, "HANDSHAKE_REQUEST", 17) == 0) {
                        printf("[SERVER] Handshake Request received.\n");
                        int clientIndex = checkClients(SenderAddr, clients);

                        if (clientIndex == -1) {
                            int newClientIndex = -1;
                            for (int i = 0; i < MaxClients; i++) {
                                if (!clients[i].active) {
                                    newClientIndex = i; break;
                                }
                            }

                            if (newClientIndex != -1) {
                                int seed = 12345;
                                // prepare handshake response (send_sequenced_message will ensure reply tag)
                                snprintf(response, sizeof(response), "message_type: HANDSHAKE_RESPONSE\nseed: %d", seed);

                                clients[newClientIndex].addr = SenderAddr;
                                clients[newClientIndex].active = true;
                                clients[newClientIndex].isSpectator = false;
                                clients[newClientIndex].addr_len = SenderAddrSize;

                                JoinerADDR = SenderAddr;
                                JoinerAddrSize = SenderAddrSize;
                                joinerActive = true;

                                printf("[SERVER] New JOINER connected (Index %d).\n", newClientIndex);
                                send_sequenced_message(server_socket, response, &SenderAddr, SenderAddrSize);
                            } else {
                                printf("[SERVER] Max clients reached.\n");
                            }
                        }
                    }
<<<<<<< HEAD
                    else if (strncmp(msg, "SPECTATOR_REQUEST", 17) == 0) {
                        printf("[SERVER] Spectator Request received.\n");
                        SpectatorADDR = SenderAddr;
=======

                    if (newClientIndex != -1) {
                        int seed = 12345;
                        sprintf(response, "message_type: HANDSHAKE_RESPONSE\nseed: %d", seed);
                        
                        // Store new client's address and set as active
                        clients[newClientIndex].addr = SenderAddr;
                        clients[newClientIndex].active = true;
                        clients[newClientIndex].isSpectator = false;
                        clients[newClientIndex].addr_len = SenderAddrSize;
                        
                        // **Print the address here for the new connection**
                        char *sender_ip = inet_ntoa(SenderAddr.sin_addr);
                        int sender_port = ntohs(SenderAddr.sin_port);
                        printf("[SERVER] New Player Index %d. Address: %s, Port: %d\n", newClientIndex, sender_ip, sender_port);
                        
                        send_sequenced_message(server_socket, response, &SenderAddr, SenderAddrSize);
                    } else {
                        printf("[SERVER] Max clients reached. Dropping HANDSHAKE_REQUEST.\n");
                    }
                } else {
                    printf("[SERVER] Known client (Index %d) re-sent HANDSHAKE_REQUEST.\n", clientIndex);
                }
            }
            else if (strcmp(msg, "SPECTATOR_REQUEST") == 0) {
                printf("[SERVER] Spectator Request received.\n");
                
                int clientIndex = checkClients(SenderAddr, clients);
                
                if (clientIndex == -1) {
                    // Find the next available slot for a new client (Spectator)
                    int newClientIndex = -1;
                    for (int i = 0; i < MaxClients; i++) {
                        if (!clients[i].active) {
                            newClientIndex = i;
                            break;
                        }
                    }

                    if (newClientIndex != -1) {
                        // Save the Spectator's address globally for updates
                        SpectatorADDR = SenderAddr; 
>>>>>>> origin/csnetwk1
                        SpectatorAddrSize = SenderAddrSize;
                        spectActive = true;
                        snprintf(response, sizeof(response), "message_type: SPECTATOR_RESPONSE");
                        send_sequenced_message(server_socket, response, &SpectatorADDR, SpectatorAddrSize);
                    }
                    else if(strncmp(msg, "BATTLE_SETUP", 12) == 0){
                        printf("[SERVER] Battle Setup received.\n");
                        // forward to spectator (send_sequenced_message tags automatically)
                        spectatorUpdateReliable(receive, server_socket);
                        promptPrinted = false;
                    }
                    else {
                        printf("\n[SERVER RECV] %s\n", msg);
                        spectatorUpdateReliable(receive, server_socket);
                    }
                }
            }
<<<<<<< HEAD
=======
            else if(strncmp(msg, "BATTLE_SETUP", 12) == 0){
                printf("[SERVER] Battle Setup received\n");
                int index = checkClients(SenderAddr,clients);
                 int senderIndex = checkClients(SenderAddr, clients);

    // 2. Relay message to all other active players (excluding sender and spectators)
    for (int i = 0; i < MaxClients; i++) {
        if (clients[i].active && i != senderIndex && !clients[i].isSpectator) {
            int sent = sendto(server_socket, receive, ByteReceived, 0,
                              (struct sockaddr*)&clients[i].addr,
                              clients[i].addr_len);
            if (sent == SOCKET_ERROR) {
                printf("[SERVER] Failed to relay to player %d: %d\n", i, WSAGetLastError());
            } else {
                printf("[SERVER] Relayed message to player %d\n", i);
            }
        }
    }

    // 3. Forward message to spectator (if any)
    spectatorUpdateReliable(receive, server_socket);

    // Optional: debug prints for protocol messages
    if (strncmp(msg, "ATTACK_ANNOUNCE", 15) == 0) {
        printf("[SERVER] Attack announced from player %d\n", senderIndex);
    } else if (strncmp(msg, "DEFENSE_ANNOUNCE", 16) == 0) {
        printf("[SERVER] Defense announced from player %d\n", senderIndex);
    } else if (strncmp(msg, "CALCULATION_REPORT", 18) == 0) {
        printf("[SERVER] Calculation report received from player %d\n", senderIndex);
    } else if (strncmp(msg, "CALCULATION_CONFIRM", 19) == 0) {
        printf("[SERVER] Calculation confirm received from player %d\n", senderIndex);
    }
                if (index != -1) {
                    char *sender_ip = inet_ntoa(SenderAddr.sin_addr);
                    int sender_port = ntohs(SenderAddr.sin_port);
                    printf("[SERVER] Battle Setup from known client Index %d. Address: %s, Port: %d\n", index, sender_ip, sender_port);

                   
        // Extract communication mode
        char *cmode = strstr(receive, "communication_mode: ");
        if (cmode) sscanf(cmode, "communication_mode: %s", clients[index].battlesetup.communicationMode);

        // Extract Pokemon name
        char *pname = strstr(receive, "pokemon_name: ");
        if (pname) sscanf(pname, "pokemon_name: %s", clients[index].battlesetup.pokemonName);

        // Extract stat boosts
        char *stats = strstr(receive, "stat_boosts: ");
        if (stats) {
            sscanf(stats, "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }",
                   &clients[index].battlesetup.boosts.specialAttack,
                   &clients[index].battlesetup.boosts.specialDefense);
        }

        // Print formatted Battle Setup
        printf("          Pokemon: %s\n", clients[index].battlesetup.pokemonName);
        printf("          Mode   : %s\n", clients[index].battlesetup.communicationMode);
        printf("          Boosts : ATK=%d, DEF=%d\n",
               clients[index].battlesetup.boosts.specialAttack,
               clients[index].battlesetup.boosts.specialDefense);
     

                    // Forward to spectator (if active)
                    spectatorUpdateReliable(receive, server_socket);
                } else {
                    printf("[SERVER] Battle Setup received from UNKNOWN client. Ignoring.\n");
                }
            }
            else {
                // Forward message to spectator (reliably)
                spectatorUpdateReliable(receive, server_socket);
                printf("\n[SERVER] Received (%d bytes): %s\n", ByteReceived, receive);
            }
>>>>>>> origin/csnetwk1
        }

retransmit_check:
        if (ctx.currentState != lastState || ctx.isMyTurn != lastTurn) {
            promptPrinted = false;
            lastState = ctx.currentState;
            lastTurn = ctx.isMyTurn;
        }
        //  RETRANSMISSION LOGIC
        if (pending.occupied) {
            DWORD now = GetTickCount();
            if ((now - pending.last_sent_ms) >= RESEND_TIMEOUT_MS) {
                if (pending.retries >= MAX_RETRIES) {
                    printf("[SERVER] Message seq=%d failed max retries. Dropping.\n", pending.seq);
                    pending.occupied = false;
                } else {
                    int sent = raw_sendto(server_socket, pending.data, pending.len, &pending.dest, pending.dest_len);
                    pending.retries++;
                    pending.last_sent_ms = now;
                    if (sent == SOCKET_ERROR) {
                        printf("[SERVER] Retransmit failed: %d\n", WSAGetLastError());
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
