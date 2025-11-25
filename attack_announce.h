#ifndef ATTACK_ANNOUNCE_H
#define ATTACK_ANNOUNCE_H

#include <winsock2.h>

#define ATTACK_ANNOUNCE_BUFFER 512

void sendAttackAnnounce(SOCKET sock, struct sockaddr_in *dest, const char *moveName, int seq);
int receiveAttackAnnounce(char *msg, char *moveName);

#endif
