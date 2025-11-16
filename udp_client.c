#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <conio.h>

#pragma comment(lib,"ws2_32.lib")

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

// Extract message after "message_type: "
char *get_message_type(char *message) {
    char *msg = strstr(message, "message_type: ");
    if (msg) return msg + 14;
    return NULL;
}

// Remove trailing newline
void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') str[len - 1] = '\0';
}

int main() {
    WSADATA wsa;
    SOCKET socket_network;
    struct sockaddr_in server_address, from_server;
    int from_len = sizeof(from_server);
    BattleSetupData setup;
    char receive[MaxBufferSize];
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

    printf("Type HANDSHAKE_REQUEST or SPECTATOR_REQUEST\n");
    printf("To chat in spectator mode, press any key in your keyboard\n");
    printf("message_type: ");
    // Send initial request
    if (fgets(buffer, MaxBufferSize, stdin) != NULL) {
        clean_newline(buffer);
        sprintf(full_message,"message_type: %s", buffer);
        sendto(socket_network, full_message, strlen(full_message), 0, (SOCKADDR*)&server_address, sizeof(server_address));
    }


    while (1) {
        /* -----------------------------
           1. Check for incoming messages
        ------------------------------ */
        fd_set readfds;
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(socket_network, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10 ms

        int activity = select(0, &readfds, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR) {
            printf("select() error\n");
            break;
        }

        if (FD_ISSET(socket_network, &readfds)) {
            memset(receive, 0, MaxBufferSize);
            int byte_received = recvfrom(socket_network, receive, sizeof(receive), 0, (SOCKADDR*)&from_server, &from_len);

            if (byte_received > 0) {
                receive[byte_received] = '\0';
                char *msg = get_message_type(receive);
                if (msg) {
                    if (!strncmp(msg, "HANDSHAKE_RESPONSE", 19)) {
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
        }

        /* -----------------------------
           2. Check for keyboard input
        ------------------------------ */
        if (isSpectator) {
            if (_kbhit()) {
                printf("\nSpectator (Chat only): ");
                if (fgets(buffer, MaxBufferSize, stdin) == NULL) continue;
                clean_newline(buffer);
                if (strlen(buffer) == 0) continue;

                sprintf(full_message, "message_type: CHAT_MESSAGE %s", buffer);
                sendto(socket_network, full_message, strlen(full_message), 0, (SOCKADDR*)&server_address, sizeof(server_address));
                printf("\n[SENT] %s\n", full_message);
            }
        }
        else {
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

                    // --- Send message ---
                    if (strcmp(setup.communicationMode, "BROADCAST") == 0) {
                        struct sockaddr_in broadcastAddr;
                        broadcastAddr.sin_family = AF_INET;
                        broadcastAddr.sin_port = htons(9002);
                        broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

                        int broadcastEnable = 1;
                        setsockopt(socket_network, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));

                        sendto(socket_network, full_message, strlen(full_message), 0, (SOCKADDR*)&broadcastAddr, sizeof(broadcastAddr));
                        printf("\n[BROADCAST SENT]\n%s\n", full_message);
                    } else {
                        sendto(socket_network, full_message, strlen(full_message), 0, (SOCKADDR*)&server_address, sizeof(server_address));
                        printf("\n[P2P SENT]\n%s\n", full_message);
                    }
                    break; // exit BATTLE_SETUP loop
                }
            }
            else {
                sprintf(full_message, "message_type: %s", buffer);
                sendto(socket_network, full_message, strlen(full_message), 0, (SOCKADDR*)&server_address, sizeof(server_address));
                printf("\n[SENT] %s\n", full_message);
            }
        }
    }

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
