#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#define MAX_BUFFER 256

void sendDefenseAnnounce(SOCKET sock, struct sockaddr_in *dest, int seq) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg),
        "message_type: DEFENSE_ANNOUNCE\n"
        "sequence_number: %d",
        seq
    );
    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)dest, sizeof(*dest));
}

int receiveDefenseAnnounce(char *msg, char *defenseName, int *blockValue) {
    return 1;
}
