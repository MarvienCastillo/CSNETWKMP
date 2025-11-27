#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#define MAX_BUFFER 2048

typedef struct {
    char attacker[64];
    char moveUsed[64];
    int remainingHP;
    int damageDealt;
    int defenderHP;
    char statusMessage[128];
} CalculationReport;

void sendCalculationReport(SOCKET sock, struct sockaddr_in *dest, CalculationReport *report, int seq) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg),
        "message_type: CALCULATION_REPORT\n"
        "attacker: %s\n"
        "move_used: %s\n"
        "remaining_health: %d\n"
        "damage_dealt: %d\n"
        "defender_hp_remaining: %d\n"
        "status_message: %s\n"
        "sequence_number: %d",
        report->attacker,
        report->moveUsed,
        report->remainingHP,
        report->damageDealt,
        report->defenderHP,
        report->statusMessage,
        seq
    );
    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)dest, sizeof(*dest));
}

int receiveCalculationReport(char *msg, CalculationReport *report) {
    char *line = strtok(msg, "\n");
    while (line) {
        if (strncmp(line, "attacker:", 9) == 0)
            sscanf(line + 9, "%63s", report->attacker);
        else if (strncmp(line, "move_used:", 10) == 0)
            sscanf(line + 10, "%63s", report->moveUsed);
        else if (strncmp(line, "remaining_health:", 17) == 0)
            sscanf(line + 17, "%d", &report->remainingHP);
        else if (strncmp(line, "damage_dealt:", 13) == 0)
            sscanf(line + 13, "%d", &report->damageDealt);
        else if (strncmp(line, "defender_hp_remaining:", 22) == 0)
            sscanf(line + 22, "%d", &report->defenderHP);
        else if (strncmp(line, "status_message:", 15) == 0)
            sscanf(line + 15, "%127[^\n]", report->statusMessage);
        line = strtok(NULL, "\n");
    }
    return 1;
}
