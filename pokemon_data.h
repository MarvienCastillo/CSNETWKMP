#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#define MAX_POKEMON 256
#define MAX_NAME_LEN 64
#define MAX_LINE_LEN 256

typedef struct {
    char name[64];
    int level;
    int hp;
    int attack;
    int defense;
    int sp_attack;    // changed from specialAttack
    int sp_defense;   // changed from specialDefense
    char type1[16];
    char type2[16];
} Pokemon;

void clean_newline(char *str);

// Load Pokémon CSV
void loadPokemonCSV(const char *filename);

// Find Pokémon by name
Pokemon* getPokemonByName(const char *name);

// Helper: extract value from "key: value" lines
void extract_value(char *msg, const char *key, char *out);

#endif
