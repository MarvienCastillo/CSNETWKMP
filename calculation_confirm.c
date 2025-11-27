#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#define MAX_BUFFER 256

void sendCalculationConfirm(SOCKET sock, struct sockaddr_in *dest, int seq) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg),
        "message_type: CALCULATION_CONFIRM\n"
        "sequence_number: %d",
        seq
    );
    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)dest, sizeof(*dest));
}


int receiveCalculationConfirm(char *msg, int *successFlag) {
    return 1;
}
