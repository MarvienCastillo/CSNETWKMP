// host.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <windows.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")

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

char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
    return NULL;
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

int main() {
    WSADATA wsa;
    SOCKET socket_network;
    struct sockaddr_in server_address, from_joiner;
    int from_len = sizeof(from_joiner);

    BattleSetupData setup;
    BattleSetupData joiner_setup;

    char receive[MaxBufferSize * 2];
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

    // Bind host socket to 127.0.0.1:9002 so it can receive packets
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(socket_network, (SOCKADDR*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("bind() failed with error %d\n", WSAGetLastError());
        closesocket(socket_network);
        WSACleanup();
        return 1;
    }

    printf("Host listening on 127.0.0.1:9002. Waiting for handshake request...\n");

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
            memset(receive, 0, sizeof(receive));
            from_len = sizeof(from_joiner);
            int byte_received = recvfrom(socket_network, receive, sizeof(receive) - 1, 0,
                                         (SOCKADDR*)&from_joiner, &from_len);
            if (byte_received > 0) {
                clean_newline(receive);
                char *msg = get_message_type(receive);
                if (!msg) continue;

                if (!strncmp(msg, "HANDSHAKE_REQUEST", strlen("HANDSHAKE_REQUEST"))) {
                    printf("[HOST] HANDSHAKE_REQUEST received from %s:%d\n",
                           inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port));

                    // send handshake response TO THE SENDER (from_joiner)
                    seed = 12345;
                    is_handshake_done = true;
                    sprintf(full_message, "message_type: HANDSHAKE_RESPONSE\nseed: %d\n", seed);

                    int sent = sendto(socket_network, full_message, (int)strlen(full_message), 0,
                                      (SOCKADDR*)&from_joiner, from_len);
                    if (sent == SOCKET_ERROR) {
                        printf("[HOST] sendto() failed: %d\n", WSAGetLastError());
                    } else {
                        printf("[HOST] HANDSHAKE_RESPONSE sent to %s:%d (seed=%d). Handshake complete!\n",
                               inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port), seed);
                    }
                } else if (!strncmp(msg, "BATTLE_SETUP", strlen("BATTLE_SETUP")) && is_handshake_done) {
                    printf("[HOST] BATTLE_SETUP received from joiner:\n%s\n", receive);
                    // (Optional) parse joiner's setup here into joiner_setup if desired
                    processBattleSetup(receive, &joiner_setup);
                    is_battle_started = true;
                } else {
                    printf("[HOST] Received message from Joiner: %s\n", receive);
                    // other messages ignored in this simple version
                    //printf("[HOST] Received (ignored): %s\n", receive);
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

                    // Build and send BATTLE_SETUP to known joiner address (from_joiner)
                    sprintf(full_message,
                            "message_type: BATTLE_SETUP\n"
                            "communication_mode: %s\n"
                            "pokemon_name: %s\n"
                            "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                            setup.communicationMode, setup.pokemonName,
                            setup.boosts.specialAttack, setup.boosts.specialDefense);

                    int sent = sendto(socket_network, full_message, (int)strlen(full_message), 0,
                                      (SOCKADDR*)&from_joiner, from_len);
                    if (sent == SOCKET_ERROR) {
                        printf("[HOST] sendto() failed: %d\n", WSAGetLastError());
                    } else {
                        printf("[HOST] BATTLE_SETUP sent to %s:%d\n", inet_ntoa(from_joiner.sin_addr), ntohs(from_joiner.sin_port));
                        is_battle_started = true;
                    }
                } else if (strcmp(buffer, "quit") == 0) {
                    break;
                } else {
                    printf("Invalid command. Please type BATTLE_SETUP or quit.\n");
                }
            }
        }
    }

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
