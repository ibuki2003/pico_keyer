#include <stdint.h>

typedef struct {
    uint8_t len;
    uint8_t code; // MSB first, dot=0, dash=1
} morse;

extern morse MORSE_TABLE[128];
