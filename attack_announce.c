
#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#define MAX_BUFFER 512

void sendAttackAnnounce(SOCKET sock, struct sockaddr_in *dest, const char *moveName, int seq) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg),
        "message_type: ATTACK_ANNOUNCE\n"
        "move_name: %s\n"
        "sequence_number: %d",
        moveName, seq
    );
    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)dest, sizeof(*dest));
}

int receiveAttackAnnounce(char *msg, char *moveName) {
    char *line = strtok(msg, "\n");
    while (line) {
        if (strncmp(line, "move_name:", 10) == 0)
            sscanf(line + 10, "%s", moveName);
        line = strtok(NULL, "\n");
    }
    return 1;
}
