/*Standard Headers*/
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "support.h"
/*Hardware / RP2350 Headers*/
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
#include "pico/time.h"
/*Hardware mtk3339 Headers*/
//#include "gpsdata.h"
//////////////////////////////////////////////////////////////////////////////
#define BUFSIZE 512
char strbuf[BUFSIZE];
#define PI 3.14159265358979323846

typedef enum {
    INIT = 0,
    READY = 1,
    GPS_READ = 2,
    GPS_ERROR = 3
} module_state_t;
module_state_t gps_state = INIT;

typedef struct {
    char ptmk[16];
    char time[16];
    char date[16];
    char latitude[16];
    char north_south[16];
    char longitude[16];
    char east_west[16];
    char num_sats[16];
    char sat_id[16];
    char sat_elev[16];
    char sat_azimuth[16];
    char ground_speed[16];
    char ground_course[16];
} gps_data;

gps_data gps;
// LCD Page Selection
typedef enum{
    PAGE_SPEED = 0,
    PAGE_LOCATION = 1,
    PAGE_TIME = 2
} page_t;

//PWM Variables
#define PWM_PIN0 22 
#define PWM_PIN1 23 
#define PWM_PIN2 24 
static int duty_cycle = 0; 
static int dir = 0; 
static int color = 0;


// Current LCD Page
volatile page_t current_page = PAGE_SPEED;

// Debounce for buttons
#define BUTTON_DEBOUNCE_US 150000
volatile uint64_t last_button_time_us = 0;

/*Prevent Implicit Declarations*/
void gps_periodic_irq();
void disp_page();

/*Init of all of the pins used */
const int button_1 = 21;
const int button_2 = 26; 
const int led_1 = -1;
const int led_2 = -1;
const int UART_TX_PIN = 8;
const int UART_RX_PIN = 9;
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
#define FONT_SCALE 2  // Scale factor to make text bigger (2x = double size)
uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;  // 4px spacing between lines


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

// Send a 16-bit (long) or 8-bit (not long) data value to the display (for colors and coordinates)
// Converts 16-bit value to two bytes (MSB first) and sends them
void send_spi_data16(spi_inst_t *spi, uint16_t data, bool is_long) {
    gpio_put(DISP_DC, 1);  // Data mode (DC = 1)
    gpio_put(SPI_CSn, 0);  // Assert chip select (active low)
    if(is_long){
        uint8_t buf[2] = { data >> 8, data & 0xFF };  // Split into MSB and LSB
        spi_write_blocking(spi, buf, 2); // Send both bytes
    }
    else{
        spi_write_blocking(spi, &data, 1); // Send data byte
    }
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
    send_spi_data16(spi0, 0x55, false);  // 0x55 = 16-bit color (RGB565)

    // Memory Access Control (0x36) - set orientation and color order
    send_spi_cmd(spi0, 0x36);
    send_spi_data16(spi0, 0x48, false);  // MX=1 (mirror X), RGB mode (BGR=0)

    // Display ON (0x29) - turn on the display
    send_spi_cmd(spi0, 0x29);
    sleep_ms(20);
}

// Set the drawing window on the display
// Defines the rectangular area where pixels will be written
// Parameters: (x0, y0) = top-left corner, (x1, y1) = bottom-right corner
void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    send_spi_cmd(spi0, 0x2A); // Column address set command
    send_spi_data16(spi0, x0, true); // Start column
    send_spi_data16(spi0, x1, true); // End column

    send_spi_cmd(spi0, 0x2B); // Row address set command
    send_spi_data16(spi0, y0, true); // Start row
    send_spi_data16(spi0, y1, true); // End row

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
        send_spi_data16(spi0, color, true);
    }
}

//////////////////////////////////////////////////////////////////////////////

#define FONT_SCALE 2  // Scale factor to make text bigger (2x = double size)

// Draw a single pixel (helper function)
// Sets the display window to a single pixel and writes the color
void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    tft_set_window(x, y, x, y);
    send_spi_data16(spi0, color, true);
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

// Draw a filled rectangle (box) with specified color
// Parameters: x0, y0 = top-left corner, x1, y1 = bottom-right corner, color = fill color
void tft_draw_box(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    // Set window to the rectangle area
    tft_set_window(x0, y0, x1, y1);
    
    // Calculate number of pixels
    uint16_t width = x1 - x0 + 1;
    uint16_t height = y1 - y0 + 1;
    uint32_t total_pixels = width * height;
    
    // Fill all pixels with the specified color
    for (uint32_t i = 0; i < total_pixels; i++) {
        send_spi_data16(spi0, color, true);
    }
}

// Display Speed: [value] mph in a blue box
// Parameters: x, y = position of top-left corner of the label box, speed_str = speed string to display
void display_speed(uint16_t x, uint16_t y, const char* speed_str, bool all) {
        uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t blue_color = RGB565(0, 0, 255);
    
    // Draw blue box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, blue_color);
    
    // Print "Speed:" label in the blue box
    tft_print_string(x + 10, y + 8, "Speed:", RGB565(255, 255, 255), blue_color);
    
    // Print the speed value below the box
    tft_print_string(x + 10, y + label_box_height + 10, speed_str, RGB565(0, 0, 0), RGB565(255, 255, 255));

    // Print Units
    tft_print_string(x + 150, y + label_box_height + 10, "km/h", RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Progress bar dimensions
        uint16_t progress_bar_y = y + label_box_height + 100;  // Position below speed text
        uint16_t progress_bar_height = 15;  // Height of progress bar
        uint16_t progress_bar_x_start = x + 10;
        uint16_t progress_bar_x_end = x + box_width - 11;
        uint16_t progress_bar_width = progress_bar_x_end - progress_bar_x_start + 1;
        
        // Draw progress bar background (empty bar in light gray)
        tft_draw_box(progress_bar_x_start, progress_bar_y, progress_bar_x_end, progress_bar_y + progress_bar_height - 1, RGB565(200, 200, 200));
        
        int max_speed = 150;

        // Calculate percentage filled (convert speed_str to float)
        float current_speed = atof(speed_str);
        float percentage = (current_speed / max_speed) * 100.0f;
        if (percentage > 100.0f) percentage = 100.0f;  // Cap at 100%
        if (percentage < 0.0f) percentage = 0.0f;      // Minimum 0%
        
        // Calculate filled width
        uint16_t filled_width = (uint16_t)((percentage / 100.0f) * progress_bar_width);
        
        // Draw filled portion of progress bar in blue (or green/yellow/red based on speed)
        uint16_t progress_color = blue_color;  // Default blue
        if (percentage > 80.0f) {
            progress_color = RGB565(255, 0, 0);  // Red for high speed
        } else if (percentage > 60.0f) {
            progress_color = RGB565(255, 165, 0);  // Orange for medium-high
        } else {
            progress_color = RGB565(0, 255, 0);  // Green for normal speed
        }
        
        if (filled_width > 0) {
            tft_draw_box(progress_bar_x_start, progress_bar_y, 
                        progress_bar_x_start + filled_width - 1, 
                        progress_bar_y + progress_bar_height - 1, progress_color);
        }
        tft_print_string(progress_bar_x_start, progress_bar_y + progress_bar_height + 10, "0", RGB565(0, 0, 0), RGB565(255, 255, 255));
        tft_print_string(progress_bar_x_end - 50, progress_bar_y + progress_bar_height + 10, "150", RGB565(0, 0, 0), RGB565(255, 255, 255));
    }
}

// Display Location: [lat, lon] in a red box
// Parameters: x, y = position of top-left corner of the label box, lat_str = latitude string, lon_str = longitude string
void display_location(uint16_t x, uint16_t y, const char* lat_str, const char* lat_dir, const char* lon_str, const char* lon_dir, bool all) {
    uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t red_color = RGB565(255, 0, 0);
    
    // Draw red box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, red_color);
    
    // Print "Location:" label in the red box
    tft_print_string(x + 10, y + 8, "Location:", RGB565(255, 255, 255), red_color);
    
    // Print latitude below the box
    tft_print_string(x + 10, y + label_box_height + 10, "Lat: ", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 80, y + label_box_height + 10, lat_str, RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 200, y + label_box_height + 10, lat_dir, RGB565(0, 0, 0), RGB565(255, 255, 255));
    
    // Print longitude below latitude
    tft_print_string(x + 10, y + label_box_height + 10 + line_height, "Lon: ", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 80, y + label_box_height + 10 + line_height, lon_str, RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 200, y + label_box_height + 10 + line_height, lon_dir, RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Print Compass Face
        uint16_t compass_color = RGB565(150, 75, 0);
        tft_draw_circle(120, 200, 80, compass_color);
        tft_print_string(115, 130, "N", RGB565(0, 0, 0),compass_color);
        tft_print_string(175, 195, "E", RGB565(0, 0, 0),compass_color);
        tft_print_string(115, 260, "S", RGB565(0, 0, 0),compass_color);
        tft_print_string(50, 195, "W", RGB565(0, 0, 0),compass_color);

        // Print Direction Line
        // Compass center is at (120, 200) with radius 80
        int center_x = 120;
        int center_y = 200;
        int radius = 60;  // Line length from center
        
        int x_end = center_x;  // Initialize to center (fallback)
        int y_end = center_y;  // Initialize to center (fallback)
        
        // Determine direction based on lat_dir and lon_dir strings
        char lat_char = lat_dir[0];  // Get first character (N or S)
        char lon_char = lon_dir[0];  // Get first character (E or W)
        
        if(lat_char == 'N' && lon_char == 'E'){
            // North-East: x increases (east), y decreases (north)
            x_end = center_x + radius - 10;
            y_end = center_y - radius + 10;
        }
        else if(lat_char == 'N' && lon_char == 'W'){
            // North-West: x decreases (west), y decreases (north)
            x_end = center_x - radius + 10;
            y_end = center_y - radius +10;
        }
        else if(lat_char == 'S' && lon_char == 'E'){
            // South-East: x increases (east), y increases (south)
            x_end = center_x + radius - 10;
            y_end = center_y + radius - 10;
        }
        else if(lat_char == 'S' && lon_char == 'W'){
            // South-West: x decreases (west), y increases (south)
            x_end = center_x - radius + 10;
            y_end = center_y + radius - 10;
        }
        else if(lat_char == 'N'){
            // North only
            x_end = center_x;
            y_end = center_y - radius + 10;
        }
        else if(lat_char == 'S'){
            // South only
            x_end = center_x;
            y_end = center_y + radius - 10;
        }
        else if(lon_char == 'E'){
            // East only
            x_end = center_x + radius - 10;
            y_end = center_y;
        }
        else if(lon_char == 'W'){
            // West only
            x_end = center_x - radius + 10;
            y_end = center_y;
        }
        
        // Draw the direction line from center to calculated endpoint
        tft_draw_thick_line(center_x, center_y, x_end, y_end, 6, red_color);
    }
}

// Display Time: [time_string] in a green box
// Parameters: x, y = position of top-left corner of the label box, time_str = time string to display
void display_time(uint16_t x, uint16_t y, const char* time_str, bool all) {
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t green_color = RGB565(0, 128, 0);

    int center_x = 120;
    int center_y = 200;
    
    // Draw green box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, green_color);
    
    // Print "Time:" label in the green box
    tft_print_string(x + 10, y + 8, "Time:", RGB565(255, 255, 255), green_color);
    
    // Parse Time String (assumes format like "123456" or "12:34:56")
    size_t num_substrings = 3;  // HH, MM, SS (not 5)
    char** time_arr = (char**)malloc(num_substrings * sizeof(char*));
    
    // Allocate Memory for Parsed Times
    for (size_t i = 0; i < num_substrings; ++i) {
        time_arr[i] = (char*)malloc(3 * sizeof(char)); // 2 digits + null terminator
        strncpy(time_arr[i], time_str + (i * 2), 2);
        time_arr[i][2] = '\0'; // Null-terminate the substring
    }

    // Print time string below the box
    tft_print_string(x + 10, y + label_box_height + 10, time_arr[0], RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 45, y + label_box_height + 10, ":", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 60, y + label_box_height + 10, time_arr[1], RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 95, y + label_box_height + 10,":", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 110, y + label_box_height + 10, time_arr[2], RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Print Clock Face
        tft_draw_circle(center_x, center_y, 100, green_color);
        tft_print_string(105, 105, "12", RGB565(255, 255, 255), green_color);
        tft_print_string(155, 120, "1", RGB565(255, 255, 255), green_color);
        tft_print_string(185, 155, "2", RGB565(255, 255, 255), green_color);
        tft_print_string(200, 192, "3", RGB565(255, 255, 255), green_color);
        tft_print_string(185, 229, "4", RGB565(255, 255, 255), green_color);
        tft_print_string(155, 264, "5", RGB565(255, 255, 255), green_color);
        tft_print_string(110, 280, "6", RGB565(255, 255, 255), green_color);
        tft_print_string(65, 264, "7", RGB565(255, 255, 255), green_color);
        tft_print_string(35, 229, "8", RGB565(255, 255, 255), green_color);
        tft_print_string(25, 192, "9", RGB565(255, 255, 255), green_color);
        tft_print_string(35, 155, "10", RGB565(255, 255, 255), green_color);
        tft_print_string(65, 120, "11", RGB565(255, 255, 255), green_color);
        tft_draw_circle(center_x, center_y, 6, RGB565(255, 255, 255));

        // Print Clock Hands
        int time_hour = atoi(time_arr[0]);
        int time_min = atoi(time_arr[1]);
        int time_sec = atoi(time_arr[2]);
        
        // Calculate angles in radians (need to use float/double, not int)
        // Hour hand: 12-hour format, position based on hour + minute fraction
        float hour_angle = ((time_hour % 12) * 30.0f + time_min * 0.5f) * (PI / 180.0f) - (PI / 2.0f);
        // Minute hand: position based on minutes
        float min_angle = (time_min * 6.0f) * (PI / 180.0f) - (PI / 2.0f);
        
        // Calculate hand endpoints (hour hand shorter, minute hand longer)
        int hour_radius = 50;  // Hour hand length
        int min_radius = 70;   // Minute hand length
        
        int x_hour = center_x + hour_radius * cosf(hour_angle);
        int y_hour = center_y + hour_radius * sinf(hour_angle);
        int x_min = center_x + min_radius * cosf(min_angle);
        int y_min = center_y + min_radius * sinf(min_angle);
        
        // Calculate second hand angle (seconds * 6 degrees per second)
        float sec_angle = (time_sec * 6.0f) * (PI / 180.0f) - (PI / 2.0f);
        
        // Calculate second hand endpoint (longest hand)
        int sec_radius = 85;  // Second hand length (longer than minute hand)
        int x_sec = center_x + sec_radius * cosf(sec_angle);
        int y_sec = center_y + sec_radius * sinf(sec_angle);

        // Draw clock hands with different thicknesses
        int hour_hand_width = 4;  // Thicker hour hand
        int min_hand_width = 4;   // Thinner minute hand
        int sec_hand_width = 3;   // Thinnest second hand
        
        // Draw second hand first (longest, goes on bottom layer)
        tft_draw_thick_line(center_x, center_y, x_sec, y_sec, sec_hand_width, RGB565(225, 225, 255));  // Red second hand
        
        // Draw minute hand second
        tft_draw_thick_line(center_x, center_y, x_min, y_min, min_hand_width, RGB565(255, 255, 255));
        
        // Draw hour hand third (so it appears on top)
        tft_draw_thick_line(center_x, center_y, x_hour, y_hour, hour_hand_width, RGB565(255, 255, 255));
    }

    // Free Memory of Parsed Times
    for (size_t i = 0; i < num_substrings; ++i) {
        free(time_arr[i]);
    }
    free(time_arr);
}

void display_all(const char* speed_str, const char* lat_str, const char* lat_dir, const char* lon_str, const char* lon_dir, const char* time_str){
    display_speed(10, 10, speed_str, 1);
    display_location(10, 80, lat_str, lat_dir, lon_str, lon_dir, 1);
    display_time(10, 180, time_str, 1);
}

// Helper to get a label for the current page 
const char* get_page_label(void) {
    switch (current_page) {
        case PAGE_SPEED:    return "Speed Screen";
        case PAGE_LOCATION: return "Location Screen";
        case PAGE_TIME:     return "Time Screen";
        default:            return "Unknown";
    }
}
// TODO this will not actually work as the ISR will not allow for arguments to be made
// As if this is a software-called function - FIX: seperate ISRs for each or look at how tis handled in lab
void page_sel_isr(uint gpio, uint32_t events) {
   /*Set up code + global to change page state with different variables displayed*/
    gpio_acknowledge_irq(gpio, GPIO_IRQ_EDGE_FALL);
    uint64_t now = time_us_64();
    if (now - last_button_time_us < BUTTON_DEBOUNCE_US) {
        return; // debounce
    }
    last_button_time_us = now;

    if (gpio == button_1) {
        // Next page: SPEED -> LOCATION -> TIME -> SPEED
        tft_fill_screen(RGB565(255,255,255));
        current_page = (current_page + 1) % 3;
    } else if (gpio == button_2) {
        // Previous page
        tft_fill_screen(RGB565(255,255,255));
        current_page = (current_page + 3 - 1) % 3;
    }
}

// Init all GPIO pins for page selection buttons 
void page_sel_init() {
    /*Init all gpio pins for page_sel and led*/
    // Button 1
    gpio_init(button_1);
    gpio_set_dir(button_1, GPIO_IN);
    gpio_pull_up(button_1);

    // Button 2
    gpio_init(button_2);
    gpio_set_dir(button_2, GPIO_IN);
    gpio_pull_up(button_2);

    // Register IRQ callback for button_1, and enable for both
    gpio_set_irq_enabled_with_callback(
        button_1,
        GPIO_IRQ_EDGE_FALL,
        true,
        &page_sel_isr
    );
    gpio_set_irq_enabled(
        button_2,
        GPIO_IRQ_EDGE_FALL,
        true
    );
}

uint32_t last_set_time = 0;

void timer_isr() {
    /*Setting up timer leaving my code here for reference*/
    timer0_hw->intr = 1u << 0;
    last_set_time = timer0_hw->timerawl;
    gps_periodic_irq();
    timer0_hw->alarm[0] = timer0_hw->timerawl + 2500;

    // fill in the code here to send ALL startup functions to the GPS
}

void screen_isr() {
    /*Setting up timer leaving my code here for reference*/
    timer0_hw->intr = 1u << 1;
    last_set_time = timer0_hw->timerawl;
    disp_page();
    timer0_hw->alarm[1] = timer0_hw->timerawl + 25000;   
}

void init_startup_timer() {
    /*Setting up a timer, it wont be the exact same but it should be similar for startup stuff*/
    timer0_hw->alarm[0] = 1E6;
    timer0_hw->alarm[1] = 11E5;
    irq_set_exclusive_handler(TIMER0_IRQ_0, timer_isr);
    irq_set_exclusive_handler(TIMER0_IRQ_1, screen_isr);
    timer0_hw->inte = 1u << 0;
    timer0_hw->inte |= 1u << 1;
    irq_set_enabled(TIMER0_IRQ_0, true);
    irq_set_enabled(TIMER0_IRQ_1, true);
}


void gps_parser(char* message){
    uint8_t i = 0;
    uint8_t message_type = 0;
    char **tokens = malloc(sizeof(char*)*20);
    const char delimiter[] = ",";
    tokens[0] = strtok(message, delimiter);
    while (tokens[i] != NULL)
    {
        i++;
        if (i >= 20) break;
        tokens[i] = strtok(NULL, delimiter);
    }
    
    // OK, I can't use strcomp in the case function as it isnt constant
    // I need to figure out a way that I can set this up to be a bitwise status int or smth. I think the solution is to have a
    // Set of ints above here that 

    if (strcmp(tokens[0],"$GPRMC") == 0)
    {
        message_type = 1;
    }
    else if (strcmp(tokens[0],"$GPVTG") == 0)
    {
        message_type = 2;
    }
    else if (strcmp(tokens[0],"$GPGGA") == 0)
    {
        message_type = 3;
    }
    else
    {
        return;
    }
    strcpy(gps.ptmk, tokens[0]);

    
    switch (message_type){
        case 1: // GPRMC
            strcpy(gps.ground_speed, tokens[7]);
            strcpy(gps.ground_course, tokens[8]);
            break;

        case 2: // GPVTG
            // Fill this if needed
            break;

        case 3: // GPGGA
            strcpy(gps.time, tokens[1]);
            strcpy(gps.latitude, tokens[2]); 
            strcpy(gps.north_south, tokens[3]);
            strcpy(gps.longitude, tokens[4]);
            strcpy(gps.east_west, tokens[5]);
            strcpy(gps.num_sats, tokens[6]);
            break;

        default:
            break;
    }
    free(tokens);
}

void init_uart_gps() {
    uart_init(uart1, 9600);
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(uart1, 0)); // TODO: double check naming of TX and RX PINS
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(uart1, 1)); // TODO: double check naming of TX and RX PINS
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    sleep_ms(1);
    uart_write_blocking(uart1, (const uint8_t*) "$PMTK104*37\r\n", strlen("$PMTK104*37\r\n"));
    uart_write_blocking(uart1,(const uint8_t*) "$PMTK314,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C\r\n", strlen("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C<CR><LF>"));
    /* BITWISE DEFINITION OF OUTPUT 
    0 NMEA_SEN_GLL, // GPGLL interval - Geographic Position - Latitude longitude
    1 NMEA_SEN_RMC, // GPRMC interval - Recommended Minimum Specific GNSS Sentence
    2 NMEA_SEN_VTG, // GPVTG interval - Course over Ground and Ground Speed
    3 NMEA_SEN_GGA, // GPGGA interval - GPS Fix Data
    4 NMEA_SEN_GSA, // GPGSA interval - GNSS DOPS and Active Satellites
    5 NMEA_SEN_GSV, // GPGSV interval - GNSS Satellites in View 
    EVERYTHING AFTER THIS IS IS RESERVED UNTIL THE LAST BIT
    */
}

void gps_periodic_irq() {
    // UGHUGHUGH I cant do uart_read_blocking because
    // The data length is constantly changing.
    // This means I have to do by char...
    char buf[BUFSIZE];
    //uart_read_blocking(uart1, buf, BUFSIZE);
    char curr;
    int16_t chars = BUFSIZE * 4;
    for (size_t i = 0; i < chars; ++i) {
        curr = uart_getc(uart1);
        if (curr == '\n')
        {
            break;
        }
        buf[i] = curr;
        //printf("%c");
    }
    buf[sizeof(buf) - 1] = '\0';
    //tft_print_multiline(10, 20, buf, 
                         //RGB565(255, 255, 255), RGB565(255, 0, 0), line_height);
    //printf("%s\n",buf);
    gps_parser(buf);    
    printf("%s, %s, %s, %s\n", gps.ptmk, gps.time, gps.ground_speed, gps.latitude);
}

void disp_page(){
    switch (current_page) {
        case PAGE_SPEED:   
            display_speed(10, 10, gps.ground_speed, 0);
            break;
        case PAGE_LOCATION:
            display_location(10, 10, gps.latitude, gps.north_south, gps.longitude, gps.east_west, 0);
            break;
        case PAGE_TIME: 
            display_time(10, 10, gps.time, 0);   
            break;
        default:   
            display_all(gps.ground_speed, gps.latitude, gps.north_south, gps.longitude, gps.east_west, gps.time);    
            break;
    }
}

//PWM FUNCTIONS

void init_pwm_static(uint32_t period, uint32_t duty_cycle) {
    gpio_set_function(PWM_PIN0, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN1, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN2, GPIO_FUNC_PWM);

    uint slice_num_0 = pwm_gpio_to_slice_num(PWM_PIN0);
    uint slice_num_1 = pwm_gpio_to_slice_num(PWM_PIN1);
    uint slice_num_2 = pwm_gpio_to_slice_num(PWM_PIN2);

    // Same clock divider on all slices
    pwm_set_clkdiv(slice_num_0, 150.0f);
    pwm_set_clkdiv(slice_num_1, 150.0f);
    pwm_set_clkdiv(slice_num_2, 150.0f);

    // Same period (TOP) on all slices
    pwm_set_wrap(slice_num_0, period - 1);
    pwm_set_wrap(slice_num_1, period - 1);
    pwm_set_wrap(slice_num_2, period - 1);

    // Initial duty on each pin (using correct channel for each GPIO)
    pwm_set_chan_level(slice_num_0, pwm_gpio_to_channel(PWM_PIN0), duty_cycle);
    pwm_set_chan_level(slice_num_1, pwm_gpio_to_channel(PWM_PIN1), duty_cycle);
    pwm_set_chan_level(slice_num_2, pwm_gpio_to_channel(PWM_PIN2), duty_cycle);

    // Enable all slices
    pwm_set_enabled(slice_num_0, true);
    pwm_set_enabled(slice_num_1, true);
    pwm_set_enabled(slice_num_2, true);
}

void pwm_breathing() {
    // Clear interrupt for the slice we enabled (PIN0's slice)
    uint slice_num_0 = pwm_gpio_to_slice_num(PWM_PIN0);
    pwm_hw->intr = 1u << slice_num_0;

    // 1) Read speed from GPS: gps.ground_speed is a NMEA string in knots
    float speed_setting = 0.0f;

    if (gps.ground_speed != NULL && gps.ground_speed[0] != '\0') {
        // Convert ASCII to float; ignores errors by passing NULL
        speed_setting = strtof(gps.ground_speed, NULL);
    }

    // 2) Map speed to step size (how fast we breathe)
    //    More speed -> larger step -> faster breathing
    int step = 1;  // minimum step

    if (speed_setting > 0.5f) {                // ignore tiny GPS noise
        float mph = speed_setting * 1.15078f;  // knots -> mph

        // 0–15 mph   -> step 1  (very gentle)
        // 15–30 mph  -> step 2
        // 30–45 mph  -> step 3
        // 45–60 mph  -> step 4
        // 60–75 mph  -> step 5
        // 75+ mph    -> step 6 (max)
        step = 1 + (int)(mph / 15.0f);

        if (step < 1) step = 1;
        if (step > 6) step = 6;
    }

    // 3) Breathing logic

    if (speed_setting > 0.0f) {
        if (dir == 0 && duty_cycle >= 100) {
            duty_cycle = 100;
            dir = 1;
            color = (color + 1) % 3;  // next LED
        }

        else if (dir == 1 && duty_cycle <= 0) {
            duty_cycle = 0;
            dir = 0;
        }

        if (dir == 0) {
            duty_cycle += step;
            if (duty_cycle > 100) duty_cycle = 100;
        } else {
            duty_cycle -= step;
            if (duty_cycle < 0) duty_cycle = 0;
        }
    } else {
        // --- Speed == 0: finish exhale and stay low ---

        if (duty_cycle > 0) {
            // If we were inhaling, switch to exhale
            if (dir == 0) dir = 1;

            duty_cycle -= 1;
            if (duty_cycle < 0) duty_cycle = 0;
        } else {
            // Already fully exhaled: hold off
            duty_cycle = 0;
            dir = 1;   // keep direction as "exhale"
        }
    }

    // 4) Apply duty_cycle to the currently active LED
    int pins[] = {PWM_PIN0, PWM_PIN1, PWM_PIN2};
    uint active_pin   = pins[color];
    uint slice_temp   = pwm_gpio_to_slice_num(active_pin);
    uint current_top  = pwm_hw->slice[slice_temp].top;

    uint32_t level = ((uint32_t)current_top * (uint32_t)duty_cycle) / 100u;

    pwm_set_chan_level(slice_temp, pwm_gpio_to_channel(active_pin), level);
}

void init_pwm_irq() {
    // Use the slice that drives PWM_PIN0 for the IRQ
    uint slice_num_0 = pwm_gpio_to_slice_num(PWM_PIN0);

    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pwm_breathing);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_set_irq0_enabled(slice_num_0, true);

    uint current_period = pwm_hw->slice[slice_num_0].top;

    // Start fully bright and exhaling
    duty_cycle = 100;
    dir = 1;
    color = 0;  // start on PWM_PIN0

    uint slice_num_1 = pwm_gpio_to_slice_num(PWM_PIN1);
    uint slice_num_2 = pwm_gpio_to_slice_num(PWM_PIN2);

    // Initialize all slices to full brightness at startup
    pwm_set_both_levels(slice_num_0, current_period, current_period);
    pwm_set_both_levels(slice_num_1, current_period, current_period);
    pwm_set_both_levels(slice_num_2, current_period, current_period);
}

//////////////////////////////////////////////////////////////////////////////


int main()
{
    /*Call all inits here*/
    stdio_init_all();
    init_uart_gps();
    init_startup_timer();
    init_spi();
    init_disp();
    tft_init();
    page_sel_init();

    uint32_t period = 1000;     // tune as desired
    uint32_t initial_dc = 0;    // start from 0% and let ISR drive it

    init_pwm_static(period, initial_dc);
    init_pwm_irq();
    // -------------------------------

    tft_fill_screen(RGB565(255, 255, 255));

    for(;;);
    return 0;
}