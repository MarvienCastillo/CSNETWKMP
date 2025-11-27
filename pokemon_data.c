#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pokemon_data.h"

static Pokemon pokemonList[MAX_POKEMON];
static int pokemonCount = 0;

void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len == 0) return;
    if (str[len - 1] == '\n' || str[len - 1] == '\r') str[len - 1] = '\0';
    len = strlen(str);
    if (len > 0 && str[len - 1] == '\r') str[len - 1] = '\0';
}

void loadPokemonCSV(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("Failed to open %s\n", filename);
        return;
    }

    char line[MAX_LINE_LEN];
    pokemonCount = 0;

    while (fgets(line, sizeof(line), f) && pokemonCount < MAX_POKEMON) {
        clean_newline(line);

        Pokemon *p = &pokemonList[pokemonCount];
        if (sscanf(line, "%63[^,],%d,%d,%d,%d,%d",
                p->name,
                &p->hp,
                &p->attack,
                &p->defense,
                &p->sp_attack,
                &p->sp_defense) == 6) {
            pokemonCount++;
        }
    }

    fclose(f);
}

Pokemon* getPokemonByName(const char *name) {
    for (int i = 0; i < pokemonCount; i++) {
        if (strcmp(pokemonList[i].name, name) == 0) {
            return &pokemonList[i];
        }
    }
    return NULL;
}

void extract_value(char *msg, const char *key, char *out) {
    char *pos = strstr(msg, key);
    if (!pos) { out[0] = '\0'; return; }

    pos += strlen(key);
    while (*pos == ' ' || *pos == ':' || *pos == '"' || *pos == '=') pos++;

    int i = 0;
    while (*pos != '\0' && *pos != '\n' && *pos != '"' && i < MAX_NAME_LEN - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
}

