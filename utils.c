#include <string.h>
#include <stdio.h>
#include "pokemon_data.h"

// Function definition for clean_newline
void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';  // Remove the newline character
    }
}

// Function definition for extract_value
void extract_value(char *msg, const char *key, char *output) {
    // Example implementation of extracting the value (simplified)
    char *start = strstr(msg, key);
    if (start) {
        start += strlen(key);  // Move pointer past the key
        sscanf(start, "%s", output);  // Extract the value into output
    } else {
        output[0] = '\0';  // Set output to empty if key is not found
    }
}
