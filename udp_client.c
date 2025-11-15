#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#define MaxBufferSize 1024
#pragma comment(lib,"ws2_32.lib")

// Utility to extract the string after "message_type: "
char *get_message_type(char *message){
    char *message_type = strstr(message,"message_type: ");
    if(message_type){
        return message_type + 14;
    }
    return NULL;
}

int main(){
    WSADATA wsa;
    
    // START WINSOCK CHECK
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "Fatal Error: WSAStartup failed with code %d\n", WSAGetLastError());
        return 1;
    }

    // CREATE SOCKET CHECK
    SOCKET socket_network = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (socket_network == INVALID_SOCKET) {
        fprintf(stderr, "Fatal Error: Could not create socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    printf("Socket is created!\n");

    // Address Setup (No direct error check needed here for assignment)
    struct sockaddr_in server_address;
    struct sockaddr_in from_server; 
    int add_len = sizeof(from_server);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[MaxBufferSize]; 
    char receive[MaxBufferSize];
    int byte_received;
    int message_len;
    int seed = 0;
    bool isSpectator = false;
    
    do{
        memset(receive,0,MaxBufferSize);
        
        // --- SELECT CHECK ---
        fd_set read; 
        struct timeval timeout;
        
        FD_ZERO(&read);
        FD_SET(socket_network,&read);
        printf("\nChecking if there is a message from server");
        timeout.tv_sec = 2;
        timeout.tv_usec = 2;
        
        int activity = select(0,&read,NULL,NULL,&timeout);

        if (activity == SOCKET_ERROR) {
            fprintf(stderr, "\nCritical Error: select failed with code %d\n", WSAGetLastError());
            break; // Exit loop on critical select error
        }
        
        if(activity > 0){
            printf("\nThere is an activity");
            
            // RECVFROM CHECK
            byte_received = recvfrom(socket_network,receive,sizeof(receive),0,(SOCKADDR *)&from_server,&add_len);
            
            if(byte_received == SOCKET_ERROR){
                fprintf(stderr, "Client Error: recvfrom failed with code %d\n", WSAGetLastError());
            }
            else if(byte_received > 0){
                // SUCCESS: Message received
                receive[byte_received] = '\0';
                char *message = get_message_type(receive);
                
                // --- Message Processing (Remains the same) ---
                if(message){
                    if(strncmp(message,"HANDSHAKE_RESPONSE",strlen("HANDSHAKE_RESPONSE")) == 0){
                        // ... (Seed extraction logic)
                        printf("\nClient: HANDSHAKE response received.\n");
                    }
                    else if(strncmp(message,"SPECTATOR_RESPONSE",strlen("SPECTATOR_RESPONSE")) == 0){
                        printf("\nClient: Joined as spectator.\n");
                        isSpectator = true;
                    }
                    else{
                        printf("\nClient: Received Battle Update: %s\n", message);
                    }
                }
                
                continue; // Go back to check for more queued data
            }
            // If byte_received == 0, it generally means success with no data, fall through.
        }
        else{
            printf("\nNo activity");
        }
        
        // --- INPUT STREAM RECOVERY ---
        if (feof(stdin) || ferror(stdin)) {
            clearerr(stdin); 
        }

        // --- USER INPUT ---
        if(!isSpectator){
            printf("\nmessage_type: ");
        }
        else{
            printf("\nSpectator (Chat Only): \n");
        }
        
        char *input_status = fgets(buffer, MaxBufferSize, stdin);
        
        if (input_status == NULL) {
            // Unrecoverable input error (EOF/critical failure)
            fprintf(stderr, "Client Input Warning: Input stream failure. Exiting loop.\n");
            break; // Use break here as the terminal stream is dead
        }
        
        // Serialization
        message_len = strlen(buffer);
        if (message_len > 0 && buffer[message_len - 1] == '\n') {
            buffer[message_len - 1] = '\0';
            message_len--;
        }

        char full_message[MaxBufferSize + 16]; 
        sprintf(full_message, "message_type: %s", buffer);
        
        // --- SENDTO CHECK ---
        printf("\nClient: Data to be sent: %s\n", full_message);
        printf("\nSending data....");
        
        message_len = strlen(full_message);
        
        // Check if the message is empty (e.g., user just hit Enter)
        if (message_len > 0) {
            if (sendto(socket_network, full_message, message_len, 0, (SOCKADDR *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
                fprintf(stderr, "Client Send Error: sendto failed with code %d\n", WSAGetLastError());
            } else {
                printf("SUCCESS\n");
            }
        } else {
            printf("SKIPPED (Empty message)\n");
        }
        
        fflush(stdin); // Non-standard clean-up

    } while(1); 
    
    // CLEANUP CHECK
    if (closesocket(socket_network) == SOCKET_ERROR) {
        fprintf(stderr, "Cleanup Error: closesocket failed with code %d\n", WSAGetLastError());
    }
    if (WSACleanup() == SOCKET_ERROR) {
        fprintf(stderr, "Cleanup Error: WSACleanup failed with code %d\n", WSAGetLastError());
    }
    return 0;
}