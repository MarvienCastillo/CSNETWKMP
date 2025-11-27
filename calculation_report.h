#ifndef CALCULATION_REPORT_H
#define CALCULATION_REPORT_H

#include <winsock2.h>

#define CALC_REPORT_BUFFER 2048

typedef struct {
    char attacker[64];
    char moveUsed[64];
    int remainingHP;
    int damageDealt;
    int defenderHP;
    char statusMessage[128];
   int damage;


} CalculationReport;

void sendCalculationReport(SOCKET sock, struct sockaddr_in *dest, CalculationReport *report, int seq);
int receiveCalculationReport(char *msg, CalculationReport *report);

#endif
