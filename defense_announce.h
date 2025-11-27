#ifndef DEFENSE_ANNOUNCE_H
#define DEFENSE_ANNOUNCE_H

#include <winsock2.h>

#define DEFENSE_ANNOUNCE_BUFFER 256

void sendDefenseAnnounce(SOCKET sock, struct sockaddr_in *dest, int seq);
int receiveDefenseAnnounce(char *msg, char *defenseName, int *blockValue);

#endif
