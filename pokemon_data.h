#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // Required for isspace and tolower, though we will use _stricmp

// Include this for Windows-specific case-insensitive compare, if needed
#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp // Map POSIX strcasecmp to Windows _stricmp
#else
// Define strcasecmp for non-Windows systems if it's not present
#include <strings.h>
#endif


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

/* ------------------------------------------
   Utility to trim trailing whitespace
------------------------------------------- */
// This function removes trailing spaces, tabs, carriage returns, and newlines.
static void trim_trailing_whitespace(char *str) {
    size_t len = strlen(str);
    // Loop backward, checking for common trailing whitespace/control characters
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}


/* ------------------------------------------
   getPokemonByName
------------------------------------------- */
static Pokemon* getPokemonByName(const char* name) {
    for (int i = 0; i < pokemon_count; i++) {
        // FIX: Use case-insensitive string comparison (strcasecmp or _stricmp)
        if (strcasecmp(pokedex[i].name, name) == 0)
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
        
        // Remove existing newline/carriage return before tokenizing (optional but good practice)
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
                case 31: 
                    // FIX: Trim whitespace from the token before storage
                    trim_trailing_whitespace(token); 
                    strncpy(p.name, token, sizeof(p.name)); 
                    p.name[sizeof(p.name) - 1] = '\0'; // Ensure null termination
                    break;
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
        printf("Name: %s, HP: %d, Atk: %d\n", pokedex[i].name, pokedex[i].hp, pokedex[i].attack);
    }
    */

    return pokemon_count;
}
#endi
