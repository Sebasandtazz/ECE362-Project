#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "support.h"

// Base library headers ncluded for your convenience.
// ** You may have to add more depending on your practical. **
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "pico/rand.h"
#include "font.h"

//void grader();

//////////////////////////////////////////////////////////////////////////////

// Make sure to set your pins if you are using this on your own breadboard.
// For the Platform Test Board, these are the correct pin numbers.
const int SPI_SCK = 18;
const int SPI_CSn = 17;
const int SPI_TX = 19;
const int SPI_RX = 16;
const int DISP_DC = 20;
const int DISP_RST = 21;

// screen dimensions 
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// RGB565 color format helper
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))
#define BGR565(r, g, b) (((b & 0xF8) << 8) | ((g & 0xFC) << 3) | ((r & 0xF8) >> 3)) 

//////////////////////////////////////////////////////////////////////////////

// Initialize SPI interface for communication with TFT display
// Sets up SPI pins and configures SPI0 at 10MHz
void init_spi() {
    uint pins[] = {SPI_SCK, SPI_CSn, SPI_TX, SPI_RX};
    for(int i = 0; i<4; i++){
        uint gpio = pins[i];
        gpio_init(gpio); // Initialize GPIO pin
        gpio_set_function(gpio, GPIO_FUNC_SPI); // Set pin to SPI function
    }
    spi_init(spi0, 10 * 1000 * 1000); // Initialize SPI0 at 10MHz
    spi_set_format(spi0, 8, 0, 0, SPI_MSB_FIRST); // 8 bits per transfer, MSB first
}

// Initialize display control pins (DC and RST)
// Sets up GPIO pins for display data/command and reset control
void init_disp() {
    gpio_init(DISP_DC);  // Initialize Data/Command pin
    gpio_init(DISP_RST); // Initialize Reset pin

    gpio_set_dir(DISP_RST, true); // Set reset pin as output
    gpio_set_dir(DISP_DC, true);  // Set data/command pin as output

    gpio_put(DISP_DC, 0);  // Set known initial value (command mode)
    gpio_put(DISP_RST, 1); // Set known initial value (not reset)
}

//////////////////////////////////////////////////////////////////////////////

// Send a command byte to the display
// Sets DC pin low to indicate command mode, then sends the command
void send_spi_cmd(spi_inst_t *spi, uint8_t cmd) {
    gpio_put(DISP_DC, 0);  // Command mode (DC = 0)
    gpio_put(SPI_CSn, 0);  // Assert chip select (active low)

    spi_write_blocking(spi, &cmd, 1); // Send command byte

    gpio_put(SPI_CSn, 1);  // Deassert chip select
}

// Send a data byte to the display
// Sets DC pin high to indicate data mode, then sends the data
void send_spi_data(spi_inst_t *spi, uint8_t data) {
    gpio_put(DISP_DC, 1);  // Data mode (DC = 1)
    gpio_put(SPI_CSn, 0);  // Assert chip select (active low)

    spi_write_blocking(spi, &data, 1); // Send data byte

    gpio_put(SPI_CSn, 1);  // Deassert chip select
}

// Send a 16-bit data value to the display (for colors and coordinates)
// Converts 16-bit value to two bytes (MSB first) and sends them
void send_spi_data16(spi_inst_t *spi, uint16_t data) {
    uint8_t buf[2] = { data >> 8, data & 0xFF };  // Split into MSB and LSB
    gpio_put(DISP_DC, 1);  // Data mode (DC = 1)
    gpio_put(SPI_CSn, 0);  // Assert chip select (active low)
    spi_write_blocking(spi, buf, 2); // Send both bytes
    gpio_put(SPI_CSn, 1);  // Deassert chip select
}

//////////////////////////////////////////////////////////////////////////////

// Initialize the TFT display
// Performs hardware reset, software reset, and configures display settings
void tft_init() {
    // Hardware reset: pulse reset pin low then high
    gpio_put(DISP_RST, 0);
    sleep_ms(50);
    gpio_put(DISP_RST, 1);
    sleep_ms(120);

    // Software reset command (0x01)
    send_spi_cmd(spi0, 0x01);
    sleep_ms(120);

    // Sleep out command (0x11) - wake display from sleep mode
    send_spi_cmd(spi0, 0x11);
    sleep_ms(120);

    // Pixel format set (0x3A) - configure color depth
    send_spi_cmd(spi0, 0x3A);
    send_spi_data(spi0, 0x55);  // 0x55 = 16-bit color (RGB565)

    // Memory Access Control (0x36) - set orientation and color order
    send_spi_cmd(spi0, 0x36);
    send_spi_data(spi0, 0x48);  // MX=1 (mirror X), RGB mode (BGR=0)

    // Display ON (0x29) - turn on the display
    send_spi_cmd(spi0, 0x29);
    sleep_ms(20);
}

// Set the drawing window on the display
// Defines the rectangular area where pixels will be written
// Parameters: (x0, y0) = top-left corner, (x1, y1) = bottom-right corner
void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    send_spi_cmd(spi0, 0x2A); // Column address set command
    send_spi_data16(spi0, x0); // Start column
    send_spi_data16(spi0, x1); // End column

    send_spi_cmd(spi0, 0x2B); // Row address set command
    send_spi_data16(spi0, y0); // Start row
    send_spi_data16(spi0, y1); // End row

    send_spi_cmd(spi0, 0x2C); // Memory write command (ready to receive pixel data)
}

// Fill the entire screen with a solid color
// This clears the screen and sets it to the specified background color
void tft_fill_screen(uint16_t color) {
    // Set window to cover entire screen
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    
    // Fill all pixels with the specified color
    // Total pixels = width * height
    uint32_t total_pixels = TFT_WIDTH * TFT_HEIGHT;
    for (uint32_t i = 0; i < total_pixels; i++) {
        send_spi_data16(spi0, color);
    }
}

//////////////////////////////////////////////////////////////////////////////

#define FONT_SCALE 2  // Scale factor to make text bigger (2x = double size)

// Draw a single pixel (helper function)
// Sets the display window to a single pixel and writes the color
void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    tft_set_window(x, y, x, y);
    send_spi_data16(spi0, color);
}

// Draw a scaled pixel block (makes text bigger)
// Draws a FONT_SCALE x FONT_SCALE block of pixels at position (x, y)
void tft_draw_scaled_pixel(uint16_t x, uint16_t y, uint16_t color) {
    for (uint8_t i = 0; i < FONT_SCALE; i++) {
        for (uint8_t j = 0; j < FONT_SCALE; j++) {
            tft_draw_pixel(x + i, y + j, color);
        }
    }
}

// Draw a single character at position (x, y) with scaling
// Reads the font bitmap and draws each pixel scaled up
void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    const unsigned char* char_data = get_char_data(c);
    
    // Draw each row of the font
    for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
        unsigned char row_data = char_data[row];
        // Draw each column (bit) in the row
        for (uint8_t col = 0; col < FONT_WIDTH; col++) {
            // Check if bit is set (MSB first, so bit 7 is leftmost)
            // 0x80 >> col extracts each bit from left to right
            if (row_data & (0x80 >> col)) {
                // Draw foreground color (scaled)
                tft_draw_scaled_pixel(x + (col * FONT_SCALE), y + (row * FONT_SCALE), color);
            } else {
                // Draw background color (scaled)
                tft_draw_scaled_pixel(x + (col * FONT_SCALE), y + (row * FONT_SCALE), bg_color);
            }
        }
    }
}

// Print a string starting at position (x, y)
// Iterates through each character and draws it with proper spacing
void tft_print_string(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg_color) {
    uint16_t x_pos = x;
    // Draw each character in the string
    while (*str) {
        tft_draw_char(x_pos, y, *str++, color, bg_color);
        // Move to next character position (scaled width + spacing)
        x_pos += (FONT_WIDTH * FONT_SCALE) + 1;
    }
}

// Print a string with automatic line wrapping at word boundaries
// Breaks text into multiple lines, wrapping at spaces/punctuation instead of mid-word
// Parameters: start_x, start_y = starting position, line_height = spacing between lines
void tft_print_multiline(uint16_t start_x, uint16_t start_y, const char* str, 
                         uint16_t color, uint16_t bg_color, uint16_t line_height) {
    uint16_t x = start_x;
    uint16_t y = start_y;
    uint16_t char_width = (FONT_WIDTH * FONT_SCALE) + 1;  // Width of one character
    uint16_t max_chars_per_line = (TFT_WIDTH - start_x - 10) / char_width;  // Max chars per line (with margin)
    
    const char* word_start = str;  // Start of current word
    uint16_t word_length = 0;      // Length of current word
    uint16_t line_chars = 0;       // Characters on current line
    
    while (*str) {
        // Check if current character is a space, comma, or period (word boundary)
        if (*str == ' ' || *str == ',' || *str == '.') {
            // Check if the word + space fits on current line
            if (line_chars + word_length + 1 > max_chars_per_line && line_chars > 0) {
                // Word doesn't fit, move to next line
                x = start_x;
                y += line_height;
                line_chars = 0;
            }
            
            // Draw the word
            while (word_start <= str) {
                tft_draw_char(x, y, *word_start++, color, bg_color);
                x += char_width;
                line_chars++;
            }
            
            word_start = str + 1;  // Next word starts after this space/punctuation
            word_length = 0;
        } else {
            // Part of current word
            word_length++;
        }
        str++;
    }
    
    // Draw any remaining word at the end
    if (word_length > 0) {
        // Check if it fits on current line
        if (line_chars + word_length > max_chars_per_line && line_chars > 0) {
            x = start_x;
            y += line_height;
        }
        
        // Draw the remaining word
        while (*word_start) {
            tft_draw_char(x, y, *word_start++, color, bg_color);
            x += char_width;
        }
    }
}

//////////////////////////////////////////////////////////////////////////////

int main()
{
    stdio_init_all();
    
    // Initialize SPI communication
    init_spi();
    
    // Initialize display control pins
    init_disp();
    
    // Initialize and configure the TFT display
    tft_init();
    
    // Fill entire screen with white to clear any grey background
    tft_fill_screen(RGB565(255, 255, 255));
    
    // USER INPUT: Change this string to display any text you want
    const char* user_input = "Hello. This is 362 final project!";
    
    // Display the user input string in black text on white background with line wrapping
    // Line height = scaled font height + spacing
    uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;  // 4px spacing between lines
    tft_print_multiline(10, 20, user_input, 
                        RGB565(0, 0, 0), RGB565(255, 255, 255), line_height);
    
    while(1);
}