#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#ifndef CALCULATION_CONFIRM_H
#define CALCULATION_CONFIRM_H
#ifndef CALCULATION_REPORT_H
#define CALCULATION_REPORT_H
#ifndef ATTACK_ANNOUNCE_H
#define ATTACK_ANNOUNCE_H

#define CALC_CONFIRM_BUFFER 256
#define CALC_REPORT_BUFFER 2048
#define ATTACK_ANNOUNCE_BUFFER 512
typedef struct {
    char attacker[64];
    char moveUsed[64];
    int remainingHP;
    int damageDealt;
    int defenderHP;
    char statusMessage[128];
   int damage;


} CalculationReport;



void sendDefenseAnnounce(SOCKET sock, struct sockaddr_in *dest, int seq);
int receiveDefenseAnnounce(char *msg, char *defenseName, int *blockValue);
void sendCalculationConfirm(SOCKET sock, struct sockaddr_in *dest, int seq);

int receiveCalculationConfirm(char *msg, int *successFlag);


void sendCalculationReport(SOCKET sock, struct sockaddr_in *dest, CalculationReport *report, int seq);
int receiveCalculationReport(char *msg, CalculationReport *report);




void sendAttackAnnounce(SOCKET sock, struct sockaddr_in *dest, const char *moveName, int seq);
int receiveAttackAnnounce(char *msg, char *moveName);

#endif
#endif
#endif
