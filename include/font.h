#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// Font dimensions
#define FONT_WIDTH 8
#define FONT_HEIGHT 8

// Function to get font data for a character
// Returns pointer to 8-byte font data array for the given character
// Returns space character data for unsupported characters
const unsigned char* get_char_data(char c);

#endif
