#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#define MaxBufferSize 1024
#pragma comment(lib,"ws2_32.lib")

// global variables
bool spectActive = false;
SOCKADDR_IN SpectatorADDR;
int SpectatorAddrSize = sizeof(SpectatorADDR);
// returns the string after the message_type 
char *get_message_type(char *message){
    char *message_type = strstr(message,"message_type: ");
    if(message_type){
        return message_type + 14;
    }
    return NULL;
}

void spectatorUpdate(char *updateMessage, SOCKET socket){
    if(spectActive){
        sendto(socket,updateMessage,strlen(updateMessage),0,(SOCKADDR *)&SpectatorADDR,SpectatorAddrSize);
        printf("\nUpdate sent to the spectator");
    }
}
int main(){
    WSADATA wsa;
    char receive[MaxBufferSize];
    SOCKADDR_IN SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);
    int ByteReceived=0;
    char response[MaxBufferSize];
    

    //start Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed! Error code: %d\n", WSAGetLastError());
        return 1;
    }

    //define address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    SOCKET receiving_socket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (receiving_socket == INVALID_SOCKET){

          //Print error message
          printf("Server: Error at socket(): %ld\n", WSAGetLastError());

          // Clean up
          WSACleanup();

          // Exit with error
          return -1;
     }
    else{
        printf("Server: socket() is OK!\n");
    }

    if(bind(receiving_socket,(SOCKADDR *)&server_address,sizeof(server_address)) == SOCKET_ERROR){
        // Print error message
        printf("Server: Error! bind() failed!\n");

        // Close the socket
        closesocket(receiving_socket);

        // Do the clean up
        WSACleanup();

        // and exit with error
        return -1;
    }
    else{
        printf("Server: bind() is OK!\n");
    }

    // Print some info on the receiver(Server) side...
    getsockname(receiving_socket, (SOCKADDR *)&server_address, (int *)sizeof(receiving_socket));
    printf("Server: Receiving IP(s) used: %s\n", inet_ntoa(server_address.sin_addr));

    printf("Server: Receiving port used: %d\n", htons(server_address.sin_port));

    printf("Server: I\'m ready to receive data packages. Waiting...\n\n");
    bool flag = true;
    do{ 
        // Ensure the receive/response buffer is cleared before each read
        memset(receive,0,MaxBufferSize);
        memset(response,0,MaxBufferSize);
        ByteReceived = recvfrom(receiving_socket,receive, sizeof(receive), 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
        char *message = get_message_type(receive);
        if(message){
            if(strncmp(message,"exit",4) == 0){
                printf("Server exiting\n");
                flag = false;
            }
            else if(strcmp(message,"HANDSHAKE_REQUEST") == 0){
                printf("Server: Handshake Request has been received\n");
                int seed = 12345;
                sprintf(response,"message_type: HANDSHAKE_RESPONSE\nseed: %d", seed);
                sendto(receiving_socket,response,strlen(response),0,(SOCKADDR *)&SenderAddr,SenderAddrSize);
                printf("A Handshake response has been sent with a seed of %d", seed);
            }   
            else if(strcmp(message,"SPECTATOR_REQUEST") == 0){
                printf("Server: Spectator request has been received\n");
                SpectatorADDR = SenderAddr;
                spectActive = true;
                sprintf(response,"message_type: SPECTATOR_RESPONSE");
                sendto(receiving_socket,response,strlen(response),0,(SOCKADDR *)&SpectatorADDR,SpectatorAddrSize);
                printf("A Spectator response has been sent!");
            }  
            else {
                spectatorUpdate(message,receiving_socket);
                printf("\nServer: Received %d bytes\n", ByteReceived);
                printf("Server: The data is: %s\n", receive);
                printf("\n");
            }
        }
   } while(flag); // can be changed
   closesocket(receiving_socket);
   WSACleanup();
}