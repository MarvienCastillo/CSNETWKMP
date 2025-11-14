#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib,"ws2_32.lib")

int main(){
    WSADATA wsa;
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
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(socket_network, (struct sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("Connection failed! Error code: %d\n", WSAGetLastError());
        closesocket(socket_network);
        WSACleanup();
        return 1;
    }
}