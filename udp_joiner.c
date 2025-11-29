// joiner.c
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
            *pos++ = b64_table[(in[1] & 0x0f) << 2];
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
        table[i] = -1;
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
        if (table[c] == 0 && c != 'A')
            continue;
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
    sprintf(filename, "%s_sticker.png", sender);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("[ERROR] Cannot save sticker.\n");
        return;
    }

    fwrite(data, 1, out_len, fp);
    fclose(fp);

    printf("[STICKER] Sticker received from %s → saved as %s\n", sender, filename);
}

/*
    message - incoming message string
    This function extracts and returns the message type from the given message string.
*/
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

/*
    str - string to clean
    This function removes trailing newline and carriage return characters from the string.
*/
void clean_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len = strlen(str);
    }
}

/*
    setup - pointer to BattleSetupData to fill
    This function prompts the user for battle setup details and fills the provided structure.
*/
void getInputBattleSetup(BattleSetupData* setup) {
    printf("communication_mode (P2P/BROADCAST): ");
    if (fgets(setup->communicationMode, sizeof(setup->communicationMode), stdin) == NULL) return;
    clean_newline(setup->communicationMode);

    printf("pokemon_name: ");
    if (fgets(setup->pokemonName, sizeof(setup->pokemonName), stdin) == NULL) return;
    clean_newline(setup->pokemonName);

    char stats_boosts[MaxBufferSize];
    printf("stat_boosts (e.g., {\"special_attack_uses\": 3, \"special_defense_uses\": 2}): ");
    if (fgets(stats_boosts, MaxBufferSize, stdin) == NULL) return;
    clean_newline(stats_boosts);

    char *atk = strstr(stats_boosts, "\"special_attack_uses\": ");
    char *def = strstr(stats_boosts, "\"special_defense_uses\": ");
    if (!atk || !def) {
        printf("Invalid stat format. Use keys: \"special_attack_uses\" and \"special_defense_uses\"\n");
        return;
    }
    sscanf(atk, "\"special_attack_uses\": %d", &setup->boosts.specialAttack);
    sscanf(def, "\"special_defense_uses\": %d", &setup->boosts.specialDefense);
}

/*
    receive - incoming message string
    setup - pointer to BattleSetupData to fill
    This function processes the BATTLE_SETUP message from the other peer and fills the provided structure.
*/
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
    struct sockaddr_in server_address, from_host;
    int serverSize = sizeof(server_address);
    int from_len = sizeof(from_host);

    BattleSetupData setup;
    BattleSetupData host_setup;

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

    // Bind joiner socket to any available port so it can receive host replies
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY; // or inet_addr("127.0.0.1");
    local_addr.sin_port = htons(0); // ephemeral

    if (bind(socket_network, (SOCKADDR*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        printf("bind() failed with error %d\n", WSAGetLastError());
        closesocket(socket_network);
        WSACleanup();
        return 1;
    }

    // Host address to send handshake to
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("Joiner ready. Type HANDSHAKE_REQUEST to start handshake (or SPECTATOR_REQUEST):\n");

    while (!is_game_over) {
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
            from_len = sizeof(from_host);
            int byte_received = recvfrom(socket_network, receive, sizeof(receive) - 1, 0,
                                         (SOCKADDR*)&from_host, &from_len);
            if (byte_received > 0) {
                clean_newline(receive);

                vprint("\n[VERBOSE] Received raw message from %s:%d\n%s\n",
                inet_ntoa(from_host.sin_addr),
                ntohs(from_host.sin_port),
                receive);

                char *msg = get_message_type(receive);
                if (!msg) continue;

                if (!strncmp(msg, "HANDSHAKE_RESPONSE", strlen("HANDSHAKE_RESPONSE"))) {
                    char *seed_ptr = strstr(receive, "seed:");
                    if (seed_ptr) {
                        sscanf(seed_ptr, "seed: %d", &seed);
                    }
                    is_handshake_done = true; // <--- important
                    // remember host address for future messages
                    // from_host already filled by recvfrom
                    printf("[JOINER] HANDSHAKE_RESPONSE received from %s:%d (seed=%d)\n",
                           inet_ntoa(from_host.sin_addr), ntohs(from_host.sin_port), seed);
                } else if (!strncmp(msg, "BATTLE_SETUP", strlen("BATTLE_SETUP")) && is_handshake_done) {
                    printf("[JOINER] BATTLE_SETUP received:\n%s\n", receive);
                    processBattleSetup(receive, &host_setup);
                    is_battle_started = true;
                } else if (!strncmp(msg, "CHAT_MESSAGE", strlen("CHAT_MESSAGE"))) {
                    processChatMessage(receive);
                    continue;
                } else if (!strcmp(msg, "VERBOSE_ON")) {
                    VERBOSE_MODE = true;
                    printf("[SYSTEM] Verbose mode enabled.\n");
                }
                else if (!strcmp(msg, "VERBOSE_OFF")) {
                    VERBOSE_MODE = false;
                    printf("[SYSTEM] Verbose mode disabled.\n");
                } else {
                    // ignore other messages in this simple version
                }
            }
        }

        // Allow user to type commands
        printf("\nmessage_type: ");
        if (fgets(buffer, MaxBufferSize, stdin) != NULL) {
            clean_newline(buffer);

            if (!strncmp(buffer, "HANDSHAKE_REQUEST", strlen("HANDSHAKE_REQUEST"))) {
                printf("[JOINER] Sending HANDSHAKE_REQUEST to host...\n");
                sprintf(full_message, "message_type: HANDSHAKE_REQUEST\n");
                int sent = sendto(socket_network, full_message, (int)strlen(full_message), 0,
                                  (SOCKADDR*)&server_address, serverSize);
                if (sent == SOCKET_ERROR) {
                    printf("[JOINER] sendto() failed: %d\n", WSAGetLastError());
                } else {
                    printf("[JOINER] HANDSHAKE_REQUEST sent to %s:%d\n",
                           inet_ntoa(server_address.sin_addr), ntohs(server_address.sin_port));
                    vprint("\n[VERBOSE] Sent message (%d bytes):\n%s\n", sent, full_message);
                }
            } else if (!strncmp(buffer, "BATTLE_SETUP", strlen("BATTLE_SETUP")) && is_handshake_done) {
                getInputBattleSetup(&setup);
                // send BATTLE_SETUP to host (use server_address — host at 127.0.0.1:9002)
                sprintf(full_message,
                        "message_type: BATTLE_SETUP\n"
                        "communication_mode: %s\n"
                        "pokemon_name: %s\n"
                        "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                        setup.communicationMode, setup.pokemonName,
                        setup.boosts.specialAttack, setup.boosts.specialDefense);

                int sent = sendto(socket_network, full_message, (int)strlen(full_message), 0,
                                  (SOCKADDR*)&server_address, serverSize);
                if (sent == SOCKET_ERROR) {
                    printf("[JOINER] sendto() failed: %d\n", WSAGetLastError());
                } else {
                    printf("[JOINER] BATTLE_SETUP sent to host %s:%d\n",
                           inet_ntoa(server_address.sin_addr), ntohs(server_address.sin_port));
                    vprint("\n[VERBOSE] Sent message (%d bytes):\n%s\n", sent, full_message);
                    is_battle_started = true;
                }
            }  else if (!strcmp(buffer, "CHAT_MESSAGE")) {
                char sender[64] = "Player 2"; //TODO: see if this can be changed based on number of joiners!
                char content_type[16];

                printf("sender_name: Player 2\n");

                printf("Enter content_type (TEXT/STICKER): ");
                if (!fgets(content_type, sizeof(content_type), stdin)) continue;
                clean_newline(content_type);

                if (strcmp(content_type, "TEXT") == 0) {
                    char message_text[512];
                    printf("Enter message_text: ");
                    if (!fgets(message_text, sizeof(message_text), stdin)) continue;
                    clean_newline(message_text);

                    static int seq = 1;
                    sprintf(full_message,
                        "message_type: CHAT_MESSAGE\n"
                        "sender_name: %s\n"
                        "content_type: TEXT\n"
                        "message_text: %s\n"
                        "sequence_number: %d\n",
                        sender, message_text, seq++);

                    int sent = sendto(socket_network, full_message, strlen(full_message), 0,
                        (SOCKADDR*)&server_address, serverSize);
                    printf("[JOINER] Sent TEXT message.\n");
                    vprint("\n[VERBOSE] Sent message (%d bytes):\n%s\n", sent, full_message);

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
                    char *full_message = malloc(needed); 
                    
                    if (!full_message) { 
                        printf("[ERROR] Failed to allocate memory for message.\n"); 
                        free(b64); 
                        continue; }

                    static int seq = 1;
                    sprintf(full_message,
                        "message_type: CHAT_MESSAGE\n"
                        "sender_name: %s\n"
                        "content_type: STICKER\n"
                        "sticker_data: %s\n"
                        "sequence_number: %d\n",
                        sender, b64, seq++);

                    int sent = sendto(socket_network, full_message, strlen(full_message), 0,
                        (SOCKADDR*)&server_address, serverSize);
                    free(b64);

                    printf("[JOINER] Sent STICKER message.\n");
                    vprint("\n[VERBOSE] Sent message (%d bytes):\n%s\n", sent, full_message);
                } else {
                    printf("[ERROR] Invalid content_type. Must be TEXT or STICKER.\n");
                }
            } else if (!strcmp(buffer, "VERBOSE_ON")) {
                VERBOSE_MODE = true;
                printf("[SYSTEM] Verbose mode enabled.\n");
            }
            else if (!strcmp(buffer, "VERBOSE_OFF")) {
                VERBOSE_MODE = false;
                printf("[SYSTEM] Verbose mode disabled.\n");
            } else {
                printf("Unknown or invalid command. Use HANDSHAKE_REQUEST or BATTLE_SETUP (after handshake).\n");
            }
        }
    }

    closesocket(socket_network);
    WSACleanup();
    return 0;
}
