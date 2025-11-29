// host.c
// Host - Option A: unicast handshake + BATTLE_SETUP; broadcast for battle/chat when communication_mode == BROADCAST
// Compile on Windows (MSVC/Visual Studio). Link with Ws2_32.lib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>
#include <stdbool.h>
#pragma comment(lib, "Ws2_32.lib")

#define MAXBUF 4096
#define HOST_PORT 9006
#define BROADCAST_IP "255.255.255.255"

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32]; // "P2P" or "BROADCAST"
    char pokemonName[64];
    StatBoosts boosts;
} BattleSetupData;

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool is_handshake_done = false;
bool is_battle_started = false;
bool is_game_over = false;
bool VERBOSE_MODE = false;
SOCKET sock = INVALID_SOCKET;   // unicast
SOCKET broad_socket = INVALID_SOCKET; // broadcast listener
void vprint(const char *fmt, ...) {
    if (!VERBOSE_MODE) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ---------------- Base64 ---------------- */
char *base64_encode(const unsigned char *src, size_t len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char*)malloc(olen + 1);
    if (!out) return NULL;
    char *pos = out;
    const unsigned char *end = src + len;
    const unsigned char *in = src;

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
    for (int i = 0; i < 256; ++i) table[i] = -1;
    for (int i = 0; i < 64; ++i) table[(unsigned char)b64_table[i]] = i;

    size_t len = strlen(src);
    unsigned char *out = (unsigned char*)malloc(len + 1);
    if (!out) return NULL;

    int val = 0, valb = -8;
    size_t pos = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '=') break;
        if (c > 127) continue;
        int d = table[c];
        if (d == -1) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out[pos++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    *out_len = pos;
    return out;
}

/* ---------------- sticker saving ---------------- */
void saveSticker(const char *b64, const char *sender) {
    size_t out_len = 0;
    unsigned char *data = base64_decode(b64, &out_len);
    if (!data) {
        printf("[HOST] Failed to decode sticker base64.\n");
        return;
    }

    // Build filename safe-ish
    char filename[256];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(filename, sizeof(filename), "%s_sticker_%04d%02d%02d_%02d%02d%02d.png",
             sender ? sender : "unknown",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("[HOST] Cannot open file to save sticker.\n");
        free(data);
        return;
    }
    fwrite(data, 1, out_len, fp);
    fclose(fp);
    free(data);
    printf("[HOST] Sticker saved as %s (from %s)\n", filename, sender ? sender : "unknown");
}

/* ---------------- helpers ---------------- */
void clean_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) { s[n-1] = '\0'; n = strlen(s); }
}

char *get_message_type(char *msg) {
    char *p = strstr(msg, "message_type: ");
    if (!p) return NULL;
    return p + strlen("message_type: ");
}

/* ---------------- message processors ---------------- */
void processChatMessage(char *msg) {
    char sender[128] = {0}, type[32] = {0};
    char *p_sender = strstr(msg, "sender_name: ");
    char *p_type = strstr(msg, "content_type: ");
    if (!p_sender || !p_type) return;

    sscanf(p_sender, "sender_name: %127[^\n]", sender);
    sscanf(p_type, "content_type: %31[^\n]", type);

    if (!strcmp(type, "TEXT")) {
        char text[1024] = {0};
        char *p_text = strstr(msg, "message_text: ");
        if (!p_text) return;
        sscanf(p_text, "message_text: %1023[^\n]", text);
        printf("[CHAT] %s: %s\n", sender, text);
    } else if (!strcmp(type, "STICKER")) {
        char *p_data = strstr(msg, "sticker_data: ");
        if (!p_data) return;
        char *b64 = p_data + strlen("sticker_data: ");
        clean_newline(b64);
        saveSticker(b64, sender[0] ? sender : "unknown");
    } else {
        printf("[CHAT] Unknown content_type: %s\n", type);
    }
}

void processBattleSetup(char *msg, BattleSetupData *out) {
    char *p;
    p = strstr(msg, "communication_mode: ");
    if (p) sscanf(p, "communication_mode: %31[^\n]", out->communicationMode);
    p = strstr(msg, "pokemon_name: ");
    if (p) sscanf(p, "pokemon_name: %63[^\n]", out->pokemonName);
    p = strstr(msg, "\"special_attack_uses\": ");
    if (p) sscanf(p, "\"special_attack_uses\": %d", &out->boosts.specialAttack);
    p = strstr(msg, "\"special_defense_uses\": ");
    if (p) sscanf(p, "\"special_defense_uses\": %d", &out->boosts.specialDefense);
    printf("[HOST] Parsed BATTLE_SETUP: mode=%s, pokemon=%s, atk=%d, def=%d\n",
           out->communicationMode, out->pokemonName, out->boosts.specialAttack, out->boosts.specialDefense);
}

/* ---------------- sending helpers ---------------- */
void send_unicast(SOCKET s, const char *msg, const struct sockaddr_in *dest, int dest_len) {
    int sent = sendto(s, msg, (int)strlen(msg), 0, (SOCKADDR*)dest, dest_len);
    if (sent == SOCKET_ERROR) {
        printf("[HOST] sendto(unicast) failed: %d\n", WSAGetLastError());
    } else {
        vprint("[HOST] Sent unicast (%d bytes) to %s:%d\n", sent, inet_ntoa(dest->sin_addr), ntohs(dest->sin_port));
    }
}

void send_broadcast(SOCKET s, const char *msg) {
    struct sockaddr_in bc;
    memset(&bc, 0, sizeof(bc));
    bc.sin_family = AF_INET;
    bc.sin_port = htons(HOST_PORT); // Option A: send broadcast to port 9002
    bc.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
        printf("[HOST] setsockopt(SO_BROADCAST) failed: %d\n", WSAGetLastError());
        return;
    }

    int sent = sendto(s, msg, (int)strlen(msg), 0, (SOCKADDR*)&bc, sizeof(bc));
    if (sent == SOCKET_ERROR) {
        printf("[HOST] sendto(broadcast) failed: %d\n", WSAGetLastError());
    } else {
        vprint("[HOST] Sent broadcast (%d bytes) on port %d\n", sent, HOST_PORT);
    }
}

/*
 Unified send: if setup.communicationMode == "BROADCAST" --> broadcast
 else unicast to `peer`.
 (Host is authoritative about current communication_mode stored in my_setup)
*/
void sendMessageAuto(const char *msg,
                     struct sockaddr_in *hostAddr,
                     int hostLen,
                     BattleSetupData setup)
{
    // ---------- BROADCAST MODE ----------
    if (!strcmp(setup.communicationMode, "BROADCAST"))
    {
        struct sockaddr_in bc;
        memset(&bc, 0, sizeof(bc));
        bc.sin_family = AF_INET;
        bc.sin_port = htons(9002);                 // Host’s port (all joiners listen too)
        bc.sin_addr.s_addr = INADDR_BROADCAST;     // Correct broadcast address

        int enable = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                       (char*)&enable, sizeof(enable)) == SOCKET_ERROR)
        {
            printf("[HOST] Failed to enable SO_BROADCAST: %d\n",
                   WSAGetLastError());
            return;
        }

        int sent = sendto(sock, msg, (int)strlen(msg), 0,
                          (SOCKADDR*)&bc, sizeof(bc));

        if (sent == SOCKET_ERROR)
            printf("[HOST] Broadcast send failed: %d\n", WSAGetLastError());
        else
            printf("[HOST] Broadcast message sent → 255.255.255.255:9002\n");

        return;
    }

    // ---------- UNICAST MODE ----------
    int sent = sendto(sock, msg, (int)strlen(msg), 0,
                      (SOCKADDR*)hostAddr, hostLen);

    if (sent == SOCKET_ERROR)
        printf("[JOINER] Unicast send failed: %d\n", WSAGetLastError());
    else
        printf("[JOINER] Unicast sent → host.\n");
}


/* ---------------- main ---------------- */
int main(void) {
    WSADATA wsa;
    struct sockaddr_in addr, from;
    int from_len = sizeof(from);

    BattleSetupData my_setup;
    BattleSetupData peer_setup;
    memset(&my_setup, 0, sizeof(my_setup));
    memset(&peer_setup, 0, sizeof(peer_setup));

    char recvbuf[MAXBUF];
    char line[512];
    char fullmsg[MAXBUF];

    struct sockaddr_in last_peer; int last_peer_len = 0;
    memset(&last_peer, 0, sizeof(last_peer));

    int seed = 12345;

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    // create UDP socket and bind to INADDR_ANY:HOST_PORT (so we receive both unicast and broadcast packets sent to port 9002)
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed\n");
        WSACleanup();
        return 1;
    }

    // allow reuse (helps during development)
    {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HOST_PORT);
    addr.sin_addr.s_addr = INADDR_ANY; // crucial: bind to any so broadcast packets to 255.255.255.255:9002 arrive

    if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Host listening on 0.0.0.0:%d (receives unicast and broadcast to this port)\n", HOST_PORT);
    printf("Waiting for handshake request...\n");
    printf("Note that to continue messaging, press any key!\n");
    while (!is_game_over) {
        // Use select on socket only; for stdin we use _kbhit() + fgets as needed
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200 ms

        int rc = select(0, &readfds, NULL, NULL, &tv);
        if (rc == SOCKET_ERROR) {
            printf("select() error: %d\n", WSAGetLastError());
            break;
        }

        // socket ready
        if (FD_ISSET(sock, &readfds)) {
            memset(recvbuf, 0, sizeof(recvbuf));
            from_len = sizeof(from);
            int br = recvfrom(sock, recvbuf, sizeof(recvbuf)-1, 0, (SOCKADDR*)&from, &from_len);
            if (br > 0) {
                clean_newline(recvbuf);
                vprint("\n[VERBOSE] Received raw (%d) from %s:%d\n%s\n", br, inet_ntoa(from.sin_addr), ntohs(from.sin_port), recvbuf);

                char *mt = get_message_type(recvbuf);
                if (!mt) continue;

                // save last peer for unicast replies
                last_peer = from;
                last_peer_len = from_len;

                if (!strncmp(mt, "HANDSHAKE_REQUEST", strlen("HANDSHAKE_REQUEST"))) {
                    printf("[HOST] HANDSHAKE_REQUEST from %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                    // reply with handshake_response
                    snprintf(fullmsg, sizeof(fullmsg), "message_type: HANDSHAKE_RESPONSE\nseed: %d\n", seed);
                    sendMessageAuto(fullmsg,&last_peer, last_peer_len, my_setup);
                    is_handshake_done = true;
                    printf("[HOST] HANDSHAKE_RESPONSE sent to %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                }
                else if (!strncmp(mt, "BATTLE_SETUP", strlen("BATTLE_SETUP"))) {
                    printf("[HOST] Received BATTLE_SETUP from %s:%d\n%s\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), recvbuf);
                    // parse joiner's setup into peer_setup
                    processBattleSetup(recvbuf, &peer_setup);
                    is_battle_started = true;
                }
                else if (!strncmp(mt, "CHAT_MESSAGE", strlen("CHAT_MESSAGE"))) {
                    // chat arrived from joiner (unicast or broadcast depending on joiner)
                    processChatMessage(recvbuf);
                }
                else if (!strncmp(mt, "VERBOSE_ON", strlen("VERBOSE_ON"))) {
                    VERBOSE_MODE = true;
                    printf("[SYSTEM] Verbose enabled.\n");
                }
                else if (!strncmp(mt, "VERBOSE_OFF", strlen("VERBOSE_OFF"))) {
                    VERBOSE_MODE = false;
                    printf("[SYSTEM] Verbose disabled.\n");
                }
                else {
                    // other messages
                    printf("[HOST] Received message from %s:%d -> %s\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), recvbuf);
                }
            }
        }

        // keyboard input handling
        if (_kbhit()) {
            printf("message_type: ");
            if (!fgets(line, sizeof(line), stdin)) continue;
            clean_newline(line);
            
            if (!strcmp(line, "quit")) {
                is_game_over = true;
                continue;
            }

            // Allow host to send BATTLE_SETUP after handshake
            if (!strcmp(line, "BATTLE_SETUP") && is_handshake_done) {
                // gather fields
                printf("communication_mode (P2P/BROADCAST): ");
                if (!fgets(my_setup.communicationMode, sizeof(my_setup.communicationMode), stdin)) continue;
                clean_newline(my_setup.communicationMode);

                printf("pokemon_name: ");
                if (!fgets(my_setup.pokemonName, sizeof(my_setup.pokemonName), stdin)) continue;
                clean_newline(my_setup.pokemonName);

                char stats[256];
                printf("stat_boosts (e.g., {\"special_attack_uses\": 3, \"special_defense_uses\": 2}): ");
                if (!fgets(stats, sizeof(stats), stdin)) continue;
                clean_newline(stats);

                char *atk = strstr(stats, "\"special_attack_uses\": ");
                char *def = strstr(stats, "\"special_defense_uses\": ");
                if (!atk || !def) {
                    printf("Invalid stat format. Use keys: \"special_attack_uses\" and \"special_defense_uses\"\n");
                    continue;
                }
                sscanf(atk, "\"special_attack_uses\": %d", &my_setup.boosts.specialAttack);
                sscanf(def, "\"special_defense_uses\": %d", &my_setup.boosts.specialDefense);

                // build message
                snprintf(fullmsg, sizeof(fullmsg),
                    "message_type: BATTLE_SETUP\n"
                    "communication_mode: %s\n"
                    "pokemon_name: %s\n"
                    "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                    my_setup.communicationMode, my_setup.pokemonName,
                    my_setup.boosts.specialAttack, my_setup.boosts.specialDefense);

                // For setup we unicast to last_peer (joiner)
                if (last_peer_len > 0) {
                    sendMessageAuto(fullmsg,&last_peer, last_peer_len, my_setup);
                    is_battle_started = true;
                    printf("[HOST] BATTLE_SETUP sent to %s:%d\n", inet_ntoa(last_peer.sin_addr), ntohs(last_peer.sin_port));
                } else {
                    printf("[HOST] No known peer to send BATTLE_SETUP to (wait for HANDSHAKE_REQUEST first)\n");
                }
                continue;
            }

            // CHAT_MESSAGE (host sending)
            if (!strcmp(line, "CHAT_MESSAGE")) {
                char sender[64];
                char content_type[16];

                printf("sender_name: ");
                if (!fgets(sender, sizeof(sender), stdin)) continue;
                clean_newline(sender);

                printf("content_type (TEXT/STICKER): ");
                if (!fgets(content_type, sizeof(content_type), stdin)) continue;
                clean_newline(content_type);

                if (!strcmp(content_type, "TEXT")) {
                    char message_text[1024];
                    printf("message_text: ");
                    if (!fgets(message_text, sizeof(message_text), stdin)) continue;
                    clean_newline(message_text);

                    static int seq = 1;
                    snprintf(fullmsg, sizeof(fullmsg),
                             "message_type: CHAT_MESSAGE\nsender_name: %s\ncontent_type: TEXT\nmessage_text: %s\nsequence_number: %d\n",
                             sender, message_text, seq++);

                    // send according to my_setup (host's chosen mode)
                    sendMessageAuto(fullmsg,&last_peer, last_peer_len, my_setup);
                    printf("[HOST] Sent CHAT_MESSAGE (TEXT).\n");
                }
                else if (!strcmp(content_type, "STICKER")) {
                    char path[512];
                    printf("Path to PNG (320x320): ");
                    if (!fgets(path, sizeof(path), stdin)) continue;
                    clean_newline(path);

                    FILE *fp = fopen(path, "rb");
                    if (!fp) { printf("[HOST] Cannot open file.\n"); continue; }
                    fseek(fp, 0, SEEK_END);
                    long fsize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    unsigned char *raw = (unsigned char*)malloc(fsize);
                    if (!raw) { fclose(fp); printf("[HOST] Memory error\n"); continue; }
                    fread(raw, 1, fsize, fp);
                    fclose(fp);

                    char *b64 = base64_encode(raw, fsize);
                    free(raw);
                    if (!b64) { printf("[HOST] base64 encode failed\n"); continue; }

                    static int seq = 1;
                    size_t needed = strlen(b64) + 512;
                    char *bigmsg = (char*)malloc(needed);
                    if (!bigmsg) { free(b64); printf("[HOST] alloc fail\n"); continue; }

                    snprintf(bigmsg, needed,
                             "message_type: CHAT_MESSAGE\nsender_name: %s\ncontent_type: STICKER\nsticker_data: %s\nsequence_number: %d\n",
                             sender, b64, seq++);

                    sendMessageAuto(bigmsg,&last_peer, last_peer_len, my_setup);
                    printf("[HOST] Sent CHAT_MESSAGE (STICKER).\n");
                    free(bigmsg);
                    free(b64);
                } else {
                    printf("[HOST] Invalid content_type\n");
                }
                continue;
            }

            if (!strcmp(line, "VERBOSE_ON")) {
                VERBOSE_MODE = true;
                printf("[SYSTEM] Verbose ON\n");
                continue;
            }
            if (!strcmp(line, "VERBOSE_OFF")) {
                VERBOSE_MODE = false;
                printf("[SYSTEM] Verbose OFF\n");
                continue;
            }

            // Quick small message: treat typed line as message_type and send it
            snprintf(fullmsg, sizeof(fullmsg), "message_type: %s\n", line);
            sendMessageAuto(fullmsg,&last_peer, last_peer_len, my_setup);
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
