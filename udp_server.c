#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define MaxBufferSize 1024
#pragma comment(lib,"ws2_32.lib")

bool spectActive = false;
SOCKADDR_IN SpectatorADDR;
int SpectatorAddrSize = sizeof(SpectatorADDR);

char *get_message_type(char *message) {
    char *message_type = strstr(message, "message_type: ");
    if (message_type) {
        return message_type + 14;
    }
    return NULL;
}

void spectatorUpdate(char *updateMessage, SOCKET socket) {
    if (spectActive) {
        sendto(socket, updateMessage, strlen(updateMessage), 0,
               (SOCKADDR *)&SpectatorADDR, SpectatorAddrSize);
        printf("\n[SERVER] Sent update to spectator");
    }
}

int main() {
    WSADATA wsa;
    char receive[MaxBufferSize];
    char response[MaxBufferSize];
    SOCKADDR_IN SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);

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

    SOCKADDR_IN server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(server_socket, (SOCKADDR *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("Server: bind() failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }

    printf("Server ready. Listening on port 9002...\n");

    bool running = true;

    while (running) {
        memset(receive, 0, MaxBufferSize);
        memset(response, 0, MaxBufferSize);

        int ByteReceived = recvfrom(server_socket, receive, sizeof(receive), 0,
                                     (SOCKADDR *)&SenderAddr, &SenderAddrSize);

        if (ByteReceived <= 0) continue;
        
        receive[ByteReceived] = '\0'; 

        char *msg = get_message_type(receive);
        if (!msg) continue;

        if (strncmp(msg, "exit", 4) == 0) {
            printf("[SERVER] Exit command received.\n");
            running = false;
        }
        else if (strcmp(msg, "HANDSHAKE_REQUEST") == 0) {
            printf("[SERVER] Handshake Request received.\n"); // Prints receive confirmation

            int seed = 12345;
            sprintf(response, "message_type: HANDSHAKE_RESPONSE\nseed: %d", seed);

            // Sends response back to the client's address (SenderAddr)
            sendto(server_socket, response, strlen(response), 0,
                   (SOCKADDR *)&SenderAddr, SenderAddrSize);

        }
        else if (strcmp(msg, "SPECTATOR_REQUEST") == 0) {
            printf("[SERVER] Spectator Request received.\n");

            SpectatorADDR = SenderAddr;
            spectActive = true;

            sprintf(response, "message_type: SPECTATOR_RESPONSE");
            sendto(server_socket, response, strlen(response), 0,
                   (SOCKADDR *)&SpectatorADDR, SpectatorAddrSize);
        }
        else {
            spectatorUpdate(receive, server_socket);
            printf("\n[SERVER] Received (%d bytes): %s\n", ByteReceived, receive);
        }
    }

    closesocket(server_socket);
    WSACleanup();
    printf("Server shut down.\n");
    return 0;
}