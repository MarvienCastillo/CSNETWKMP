#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define MaxBufferSize 1024
#pragma comment(lib,"ws2_32.lib")
// returns the string after the message_type 
char *get_message_type(char *message){
    char *message_type = strstr(message,"message_type: ");
    if(message_type){
        return message_type + 14;
    }
    return NULL;
}
int main(){
    WSADATA wsa;
    SOCKADDR_IN SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);

    int TotalbyteSent;
    // start winsock
     if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed! Error code: %d\n", WSAGetLastError());
        return 1;
    }

    // create a socket
    SOCKET socket_network = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (socket_network == INVALID_SOCKET) {
        printf("Could not create socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    else
        printf("Socket is created!");

    //specify an address for the socket
    struct sockaddr_in server_address;
    struct sockaddr_in from_server; // seperate address for receiving message from the server
    int add_len = sizeof(from_server);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    char buffer[MaxBufferSize]; 
    char receive[MaxBufferSize];
    int byte_received;
    int message_len;
    int seed;
    do{
        // This checks if the client received a message from the server
        fd_set read; 
        struct timeval timeout;

        FD_ZERO(&read);
        FD_SET(socket_network,&read);

        timeout.tv_sec = 1;
        timeout.tv_usec = 1;
        
        int activity = select(0,&read,NULL,NULL,&timeout);
        if(activity>0){
            byte_received = recvfrom(socket_network,receive,sizeof(receive),0,(SOCKADDR *)&from_server,&add_len);
            if(byte_received > 0){
                receive[byte_received] = '\0';
                char *message = get_message_type(receive);
                if(strncmp(message,"HANDSHAKE_RESPONSE",strlen("HANDSHAKE_RESPONSE")) == 0)
                {
                    char *seed_ptr = strstr(message,"seed:");
                    if(seed_ptr != NULL){
                        int scanStatus = sscanf(seed_ptr,"seed: %d", &seed);
                        if (scanStatus == 1){
                            printf("Seed is %d", seed);
                        }
                    }
                    printf("\nClient: Received response/update: %s\n", message);
                }

                continue;
            }
        }

        // User Input
        printf("\nmessage_type: ");
        if (fgets(buffer, MaxBufferSize, stdin) == NULL) {
            break; // Exit loop on error
        }
        // Remove newline character if present
        message_len = strlen(buffer);
        if (message_len > 0 && buffer[message_len - 1] == '\n') {
            buffer[message_len - 1] = '\0';
            message_len--; // Adjust length to exclude the null terminator for sending
        }

        // Add the message_type: prefix for compliance (basic version)
        char full_message[MaxBufferSize + 16]; 
        sprintf(full_message, "message_type: %s", buffer);
        
        printf("Client: Data to be sent: %s\n", full_message);
        printf("Sending data....\n");
        
        message_len = strlen(full_message);
        sendto(socket_network,full_message,message_len,0,(SOCKADDR *)&server_address,sizeof(server_address));
    } while(1);
    closesocket(socket_network);
    WSACleanup();
}