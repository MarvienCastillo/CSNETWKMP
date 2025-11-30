// joiner.c - FULL VERSION (Broadcast mode with Option B1)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <conio.h>
#pragma comment(lib, "Ws2_32.lib")

#define MaxBufferSize 1024
#define MAX_SPECTATORS 10 
typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32]; // P2P or BROADCAST
    char pokemonName[64];
    StatBoosts boosts;
} BattleSetupData;

// Game state flags
bool is_handshake_done = false;
bool is_battle_started = false;
bool is_game_over = false;
bool VERBOSE_MODE = false;



SOCKET socket_network = INVALID_SOCKET;   // unicast

struct sockaddr_in broadcast_recv_addr;




// Global variables
int seed = 0;
// Add this near your existing global variables or inside main() for better scope
struct sockaddr_in spectator_list[MAX_SPECTATORS];
int spectator_count = 0;
bool isSpectator = false;
struct sockaddr_in hostAddr, from;
int fromLen = sizeof(from);
bool battle_setup_received = false;
// ----------------------------------------------------
// Verbose printing helper
// ----------------------------------------------------
void vprint(const char *fmt, ...) {
    if (!VERBOSE_MODE) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// ----------------------------------------------------
// Base64 encoder/decoder for stickers
// ----------------------------------------------------
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
    for (int i = 0; i < 256; i++) table[i] = -1;
    for (int i = 0; i < 64; i++) table[(unsigned char)b64_table[i]] = i;

    size_t len = strlen(src);
    unsigned char *out = malloc(len);

    int val = 0, valb = -8;
    size_t pos = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = src[i];

        if (c == '=' || table[c] < 0) continue;
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
        printf("[ERROR] Base64 decode failed.\n");
        return;
    }

    char filename[128];
    sprintf(filename, "%s_sticker.png", sender);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("[ERROR] Could not save sticker.\n");
        free(data);
        return;
    }

    fwrite(data, 1, out_len, fp);
    fclose(fp);
    free(data);

    printf("[STICKER] Received sticker from %s → %s\n", sender, filename);
}

// ----------------------------------------------------
// Helper functions
// ----------------------------------------------------
void clean_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 &&
          (str[len-1] == '\n' || str[len-1] == '\r')) {
        str[len-1] = '\0';
        len--;
    }
}

char *get_message_type(char *msg) {
    char *p = strstr(msg, "message_type: ");
    if (!p) return NULL;
    return p + 14;
}

// ----------------------------------------------------
// CHAT MESSAGE processor
// ----------------------------------------------------
void processChatMessage(char *msg) {
    char sender[64], type[16];

    char *p_sender = strstr(msg, "sender_name: ");
    char *p_type   = strstr(msg, "content_type: ");

    if (!p_sender || !p_type) return;

    sscanf(p_sender, "sender_name: %63[^\n]", sender);
    sscanf(p_type, "content_type: %15[^\n]", type);

    if (!strcmp(type, "TEXT")) {
        char text[512];
        char *p_text = strstr(msg, "message_text: ");

        if (!p_text) return;

        sscanf(p_text, "message_text: %511[^\n]", text);
        printf("[CHAT] %s: %s\n", sender, text);

    } else if (!strcmp(type, "STICKER")) {
        char *p_data = strstr(msg, "sticker_data: ");
        if (!p_data) return;

        saveSticker(p_data + strlen("sticker_data: "), sender);
    }
}

// ----------------------------------------------------
// BATTLE SETUP Input + Processing
// ----------------------------------------------------
void getInputBattleSetup(BattleSetupData *s) {
    printf("communication_mode (P2P/BROADCAST): ");
    fgets(s->communicationMode, sizeof(s->communicationMode), stdin);
    clean_newline(s->communicationMode);

    printf("pokemon_name: ");
    fgets(s->pokemonName, sizeof(s->pokemonName), stdin);
    clean_newline(s->pokemonName);

    char buf[256];
    printf("stat_boosts ({\"special_attack_uses\": 4, \"special_defense_uses\": 3}): ");
    fgets(buf, sizeof(buf), stdin);
    clean_newline(buf);

    char *atk = strstr(buf, "\"special_attack_uses\": ");
    char *def = strstr(buf, "\"special_defense_uses\": ");

    if (!atk || !def) {
        printf("Invalid format.\n");
        return;
    }

    sscanf(atk, "\"special_attack_uses\": %d", &s->boosts.specialAttack);
    sscanf(def, "\"special_defense_uses\": %d", &s->boosts.specialDefense);
}

void processBattleSetup(char *msg, BattleSetupData *s) {
    
    sscanf(strstr(msg, "communication_mode:"), "communication_mode: %31[^\n]",
           s->communicationMode);
    sscanf(strstr(msg, "pokemon_name:"), "pokemon_name: %63[^\n]",
           s->pokemonName);
    sscanf(strstr(msg, "\"special_attack_uses\":"), "\"special_attack_uses\": %d",
           &s->boosts.specialAttack);
    sscanf(strstr(msg, "\"special_defense_uses\":"), "\"special_defense_uses\": %d",
           &s->boosts.specialDefense);
    printf("[HOST] Parsed BATTLE_SETUP: mode=%s, pokemon=%s, atk=%d, def=%d\n",
            s->communicationMode, s->pokemonName, s->boosts.specialAttack, s->boosts.specialDefense);
}

// ----------------------------------------------------
// UNIFIED SEND FUNCTION
// P2P = unicast to host
// BROADCAST = send to 255.255.255.255
// ----------------------------------------------------
void sendMessageAuto(const char *msg,
                     struct sockaddr_in *hostAddr,
                     int hostLen,
                     BattleSetupData setup, bool isBroadcast)
{
    // BROADCAST MODE
    if(isSpectator){
        printf("========================================\n");
        printf("Received message: %s\n",msg);
        printf("========================================\n");
    }
    if (!strcmp(setup.communicationMode, "BROADCAST") || !battle_setup_received || isBroadcast) {
        struct sockaddr_in bc;
        memset(&bc, 0, sizeof(bc));
        bc.sin_family = AF_INET;
        bc.sin_port = htons(9002);            // host listens on 9002
        bc.sin_addr.s_addr =  inet_addr("255.255.255.255");

        int enable = 1;
        setsockopt(socket_network, SOL_SOCKET, SO_BROADCAST,
                   (char*)&enable, sizeof(enable));
        int sent = sendto(socket_network, msg, strlen(msg), 0,
                          (SOCKADDR*)&bc, sizeof(bc));

        if (sent == SOCKET_ERROR)
            printf("[JOINER] Broadcast send failed: %d\n", WSAGetLastError());
        else
            printf("[JOINER] Broadcast message sent.\n");

        return;
    }

    // UNICAST MODE
    int sent = sendto(socket_network, msg, strlen(msg), 0,
                      (SOCKADDR*)hostAddr, hostLen);

    if (sent == SOCKET_ERROR)
        printf("[JOINER] Unicast send failed: %d\n", WSAGetLastError());
    else
        printf("[JOINER] Unicast message sent.\n");
}
void processReceivedMessage(char *msg, struct sockaddr_in *from_addr, int from_len) {
    char *type = get_message_type(msg);
    if (!type) return;
    printf("========================================\n");
    // HANDSHAKE_RESPONSE
    if (!strncmp(type, "HANDSHAKE_RESPONSE", strlen("HANDSHAKE_RESPONSE"))) {
        char *seed_ptr = strstr(msg, "seed: ");
        if (seed_ptr) {
            sscanf(seed_ptr, "seed: %d", &seed);
        }
        is_handshake_done = true;
        printf("[JOINER] Handshake completed with host %s:%d\n",
               inet_ntoa(from_addr->sin_addr), ntohs(from_addr->sin_port));
        printf("[JOINER] Received seed: %d\n", seed);
        
    }
    else if(!strncmp(type, "SPECTATOR_RESPONSE", strlen("SPECTATOR_RESPONSE")) && isSpectator) {
        printf("[JOINER] Registered as spectator with host %s:%d\n",
               inet_ntoa(from_addr->sin_addr), ntohs(from_addr->sin_port));
    }
    // BATTLE_SETUP
    else if (!strncmp(type, "BATTLE_SETUP", strlen("BATTLE_SETUP"))) {
        BattleSetupData host_setup;
        processBattleSetup(msg, &host_setup);
        printf("[JOINER] Received BATTLE_SETUP from host.\n");
    
    }

    // CHAT_MESSAGE
    else if (!strncmp(type, "CHAT_MESSAGE", strlen("CHAT_MESSAGE"))) {
        processChatMessage(msg);
    }
    else if(!strcmp(type,"VERBOSE_ON")){
        VERBOSE_MODE = true;
        printf("\n[SYSTEM] Verbose mode enabled\n");
    }
    else if(!strcmp(type,"VERBOSE_OFF")){
        VERBOSE_MODE = false;
        printf("\n[SYSTEM] Verbose mode disabled\n");
    }
    else{
        if(isSpectator){
            printf("Received message: %s",msg);
        }
        printf("Invalid command or Spectator is receiving the message!\n");
    }
    printf("========================================\n");
}

void inputChatMessage(char *outbuf, BattleSetupData setup, struct sockaddr_in hostAddr, int hostLen) {
    char sender[] = "Player 2";
    char ctype[16];

    printf("sender_name: %s\n", sender);
    printf("content_type (TEXT/STICKER): ");
    fgets(ctype, sizeof(ctype), stdin);
    clean_newline(ctype);

    // TEXT
    if (!strcmp(ctype, "TEXT")) {

        char text[512];
        printf("message_text: ");
        fgets(text, sizeof(text), stdin);
        clean_newline(text);

        static int seq = 1;

        sprintf(outbuf,
            "message_type: CHAT_MESSAGE\n"
            "sender_name: %s\n"
            "content_type: TEXT\n"
            "message_text: %s\n"
            "sequence_number: %d\n",
            sender, text, seq++
        );

        sendMessageAuto(outbuf, &hostAddr, sizeof(hostAddr), setup,true);
        printf("[JOINER] Sent TEXT chat.\n");
    }

    // STICKER
    else if (!strcmp(ctype, "STICKER")) {

        char path[256];
        printf("PNG path: ");
        fgets(path, sizeof(path), stdin);
        clean_newline(path);

        FILE *fp = fopen(path, "rb");
        if (!fp) {
            printf("[ERROR] Cannot open PNG.\n");
            return;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        unsigned char *raw = malloc(fsize);
        fread(raw, 1, fsize, fp);
        fclose(fp);

        char *b64 = base64_encode(raw, fsize);
        free(raw);

        static int seq = 1;

        sprintf(outbuf,
            "message_type: CHAT_MESSAGE\n"
            "sender_name: %s\n"
            "content_type: STICKER\n"
            "sticker_data: %s\n"
            "sequence_number: %d\n",
            sender, b64, seq++
        );

        free(b64);

        sendMessageAuto(outbuf, &hostAddr, sizeof(hostAddr), setup,true);
        printf("[JOINER] Sent STICKER chat.\n");
    }
    else{
        printf("[ERROR] Unknown content_type.\n");
    }
}

// ----------------------------------------------------
// MAIN
// ----------------------------------------------------

int main() {
    WSADATA wsa;
    BattleSetupData setup;
    BattleSetupData host_setup;
    int spectator_count = 0;
    char receive[2048];
    char input[MaxBufferSize];
    char outbuf[2048];

    

    // Init winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // UNICAST SOCKET
    socket_network = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_network == INVALID_SOCKET) {
        printf("socket() failed.\n");
        return 1;
    }


    // ------------------------------
    // SETUP BROADCAST LISTENER SOCKET (Option B1)
    // ------------------------------

    // listening
    memset(&broadcast_recv_addr, 0, sizeof(broadcast_recv_addr));
    broadcast_recv_addr.sin_family = AF_INET;
    broadcast_recv_addr.sin_addr.s_addr = INADDR_ANY;
    broadcast_recv_addr.sin_port = htons(9003);
    int from_len = sizeof(broadcast_recv_addr);
    int optVal = 1; // Set to 1 to enable the option
    int optLen = sizeof(optVal);

    // Set the SO_REUSEADDR option
    if (setsockopt(socket_network, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, optLen) == SOCKET_ERROR)
    {
        printf("[ERROR] setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
        // Handle error, but proceed to bind attempt
    }
    if (bind(socket_network, (SOCKADDR*)&broadcast_recv_addr,
             sizeof(broadcast_recv_addr)) == SOCKET_ERROR)
    {
        printf("[ERROR] Could not bind broadcast listener port.\n");
        return 1;
    }
    int len = sizeof(broadcast_recv_addr);
    getsockname(socket_network, (SOCKADDR*)&broadcast_recv_addr, &len);
    printf("[JOINER] Listening for broadcast on port %d.\n", ntohs(broadcast_recv_addr.sin_port));

    // Host address
    memset(&hostAddr, 0, sizeof(hostAddr));
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = INADDR_ANY;
    hostAddr.sin_port = htons(0);

    // sending addr for spectator
    struct sockaddr_in spectator;
    memset(&spectator, 0, sizeof(spectator));
    spectator.sin_family = AF_INET;
    spectator.sin_family = inet_addr("255.255.255.255");
    spectator.sin_port = htons(9002);
    printf("Joiner ready. Type HANDSHAKE_REQUEST or SPECTATOR_REQUEST to start handshake.\n");
    printf("Note that to continue messaging, press any key!\n");
    while (!is_game_over) {

        // SELECT setup
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_network, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int result = select(0, &readfds, NULL, NULL, &tv);

        if (result == SOCKET_ERROR) {
            printf("select() failed.\n");
            break;
        }


        // BROADCAST RECEIVE
        if (FD_ISSET(socket_network, &readfds) && is_handshake_done) {
            memset(receive, 0, sizeof(receive));
            int rec = recvfrom(socket_network, receive, sizeof(receive)-1,
                               0, (SOCKADDR*)&broadcast_recv_addr, &from_len);

            if (rec > 0) {
                clean_newline(receive);
                processReceivedMessage(receive, &broadcast_recv_addr, fromLen);
            }
        }

        // --------------------------
        // USER INPUT SECTION
        // --------------------------
        
        if(_kbhit()) {
            printf("\nmessage_type: ");
            if(!isSpectator){
                if (!fgets(input, sizeof(input), stdin)) continue;
                    clean_newline(input);

                // HANDSHAKE_REQUEST
                if (!strcmp(input, "HANDSHAKE_REQUEST")) {
                    isSpectator = false;
                    sprintf(outbuf, "message_type: HANDSHAKE_REQUEST\n");
                    is_handshake_done = true;
                    sendMessageAuto(outbuf, &hostAddr, sizeof(hostAddr), setup,false);

                    printf("[JOINER] HANDSHAKE_REQUEST sent.\n");
                } else if(!strcmp(input, "SPECTATOR_REQUEST")) {
                    is_handshake_done = true;
                    isSpectator = true;
                    sprintf(outbuf, "message_type: SPECTATOR_REQUEST\n");

                    sendMessageAuto(outbuf, &spectator, sizeof(spectator), setup,false);

                    printf("[JOINER] SPECTATOR_REQUEST sent.\n");
                }
                // BATTLE_SETUP
                else if (!strcmp(input, "BATTLE_SETUP") && is_handshake_done) {

                    getInputBattleSetup(&setup);
                    sprintf(outbuf,
                        "message_type: BATTLE_SETUP\n"
                        "communication_mode: %s\n"
                        "pokemon_name: %s\n"
                        "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d }\n",
                        setup.communicationMode,
                        setup.pokemonName,
                        setup.boosts.specialAttack,
                        setup.boosts.specialDefense
                    );

                    sendMessageAuto(outbuf, &hostAddr, sizeof(hostAddr), setup,false);
                    battle_setup_received = true;
                    printf("[JOINER] Sent BATTLE_SETUP.\n");
                }

                // CHAT_MESSAGE
                else if (!strcmp(input, "CHAT_MESSAGE")) {
                    inputChatMessage(outbuf, setup, hostAddr, sizeof(hostAddr));
                }

                else if (!strcmp(input, "VERBOSE_ON")) {
                    VERBOSE_MODE = true;
                    printf("\n[SYSTEM] Verbose mode enabled.\n");

                    // Send to joiner
                    sprintf(outbuf, "message_type: VERBOSE_ON\n");
                    sendMessageAuto(outbuf,&hostAddr,sizeof(hostAddr),setup,true);
                    vprint("\n[VERBOSE] Sent verbose ON message to joiner %s\n", outbuf);
                }

                else if (!strcmp(input, "VERBOSE_OFF")) {
                    VERBOSE_MODE = false;
                    printf("\n[SYSTEM] Verbose mode disabled.\n");

                    // Send to joiner
                    sprintf(outbuf, "message_type: VERBOSE_OFF\n");
                    sendMessageAuto(outbuf,&hostAddr,sizeof(hostAddr),setup,true);
                    vprint("\n[VERBOSE] Sent verbose OFF message to joiner%s\n", outbuf);
                }

                else {
                    printf("[JOINER] Sending command.\n");
                    sendMessageAuto(input, &hostAddr, sizeof(hostAddr), setup,true);
                }
            }
            else{
                if (!fgets(input, sizeof(input), stdin)) continue;
                    clean_newline(input);
                
                if(!strncmp(input,"CHAT_MESSAGE",strlen("CHAT_MESSAGE"))){
                    inputChatMessage(outbuf, setup, hostAddr, sizeof(hostAddr));
                }
                else if(strncmp(input,"CHAT_MESSAGE",strlen("CHAT_MESSAGE")))
                {
                    printf("[SPECTATOR] cannot access %s command.\n",input);
                }
                else{
                    printf("[SPECTATOR] Unknown command.\n");
                }
            }
        }
    }

    closesocket(socket_network);
    closesocket(socket_network);
    WSACleanup();
    return 0;
}
