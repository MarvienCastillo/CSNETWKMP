#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include "pokemon_data.h"

#define MAX_BUFFER 2048

typedef struct {
    char communicationMode[32];
    char pokemonName[64];
    int specialAttackBoosts;
    int specialDefenseBoosts;
    Pokemon *pokemonData;
} BattleSetupData;

void sendBattleSetup(SOCKET sock, struct sockaddr_in *dest, BattleSetupData *setup, int seq) {
    char msg[MAX_BUFFER];

    if (!setup->pokemonData) {
        printf("[BATTLE_SETUP] Error: Pokemon data is NULL\n");
        return;
    }

    snprintf(msg, sizeof(msg),
        "message_type: BATTLE_SETUP\n"
        "communication_mode: %s\n"
        "pokemon_name: %s\n"
        "stat_boosts: {\"special_attack_uses\": %d, \"special_defense_uses\": %d}\n"
        "pokemon: {\"hp\": %d, \"attack\": %d, \"defense\": %d, \"sp_attack\": %d, \"sp_defense\": %d}\n"
        "sequence_number: %d",
        setup->communicationMode,
        setup->pokemonName,
        setup->specialAttackBoosts,
        setup->specialDefenseBoosts,
        setup->pokemonData->hp,
        setup->pokemonData->attack,
        setup->pokemonData->defense,
        setup->pokemonData->sp_attack,
        setup->pokemonData->sp_defense,
        seq
    );

    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)dest, sizeof(*dest));
}

int receiveBattleSetup(char *msg, BattleSetupData *setup) {
    char *line = strtok(msg, "\n");
    while (line) {
        if (strncmp(line, "communication_mode:", 19) == 0)
            sscanf(line + 19, "%31s", setup->communicationMode);
        else if (strncmp(line, "pokemon_name:", 13) == 0)
            sscanf(line + 13, "%63s", setup->pokemonName);
        else if (strncmp(line, "stat_boosts:", 12) == 0)
            sscanf(line + 12, "{\"special_attack_uses\": %d, \"special_defense_uses\": %d}", 
                   &setup->specialAttackBoosts, &setup->specialDefenseBoosts);
        else if (strncmp(line, "pokemon:", 8) == 0)
            sscanf(line + 8, "{\"hp\": %d, \"attack\": %d, \"defense\": %d, \"sp_attack\": %d, \"sp_defense\": %d}",
                   &setup->pokemonData->hp, &setup->pokemonData->attack, &setup->pokemonData->defense,
                   &setup->pokemonData->sp_attack, &setup->pokemonData->sp_defense);
        line = strtok(NULL, "\n");
    }
    return 1;
}
