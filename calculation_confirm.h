#ifndef CALCULATION_CONFIRM_H
#define CALCULATION_CONFIRM_H

#include <winsock2.h>

#define CALC_CONFIRM_BUFFER 256

void sendCalculationConfirm(SOCKET sock, struct sockaddr_in *dest, int seq);

int receiveCalculationConfirm(char *msg, int *successFlag);

#endif
