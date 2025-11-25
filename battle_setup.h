#ifndef BATTLE_SETUP_H
#define BATTLE_SETUP_H

#include <winsock2.h>
#include "pokemon_data.h"

#define BATTLE_SETUP_BUFFER 2048

typedef struct {
    int specialAttack;
    int specialDefense;
} StatBoosts;

typedef struct {
    char communicationMode[32];
    char pokemonName[64];
    StatBoosts boosts;       // <- NEW, replaces the two ints
    Pokemon *pokemonData;
} BattleSetupData;

void sendBattleSetup(SOCKET sock, struct sockaddr_in *dest, BattleSetupData *setup, int seq);

int receiveBattleSetup(char *msg, BattleSetupData *setup);

#endif
