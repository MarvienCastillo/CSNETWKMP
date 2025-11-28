#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[64];
    char type1[32];
    char type2[32];
    int hp;
    int attack;
    int defense;
    int sp_attack;
    int sp_defense;
} Pokemon;

static Pokemon pokedex[1200];
static int pokemon_count = 0;
void clean_newline(char *str);
void extract_value(char *msg, const char *key, char *output);

/* ------------------------------------------
   getPokemonByName
------------------------------------------- */
static Pokemon* getPokemonByName(const char* name) {
    for (int i = 0; i < pokemon_count; i++) {
        if (strcmp(pokedex[i].name, name) == 0)
            return &pokedex[i];
    }
    return NULL;
}

/* ------------------------------------------
   loadPokemonCSV - correct column parsing
------------------------------------------- */
static int loadPokemonCSV(const char* filename) {

    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("[POKEMON LOADER] ERROR: Cannot open %s\n", filename);
        return 0;
    }

    char line[4096];

    /// Skip header
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f)) {

        // Strip newline
        line[strcspn(line, "\r\n")] = 0;

        char* token;
        int col = 0;

        Pokemon p;
        memset(&p, 0, sizeof(Pokemon));

        token = strtok(line, ",");

        while (token != NULL) {

            switch (col) {

                case 20: p.attack = atoi(token); break;
                case 26: p.defense = atoi(token); break;
                case 27: p.hp = atoi(token); break;
                case 31: strncpy(p.name, token, sizeof(p.name)); break;
                case 34: p.sp_attack = atoi(token); break;
                case 35: p.sp_defense = atoi(token); break;
                case 37: strncpy(p.type1, token, sizeof(p.type1)); break;
                case 38: strncpy(p.type2, token, sizeof(p.type2)); break;
            }

            token = strtok(NULL, ",");
            col++;
        }

        // Empty rows or malformed rows are skipped
        if (strlen(p.name) == 0) continue;

        pokedex[pokemon_count++] = p;
    }

    fclose(f);

    /*for debugging
    printf("[POKEMON LOADER] Loaded %d Pokémon\n", pokemon_count);
    for (int i = 0; i < 3 && i < pokemon_count; i++) {
        printf("  #%d: %s (%s/%s) HP=%d ATK=%d DEF=%d SPA=%d SPD=%d\n",
            i,
            pokedex[i].name,
            pokedex[i].type1,
            pokedex[i].type2,
            pokedex[i].hp,
            pokedex[i].attack,
            pokedex[i].defense,
            pokedex[i].sp_attack,
            pokedex[i].sp_defense
        );
    }

    return 1;
} */

#endif