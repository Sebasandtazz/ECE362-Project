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
#define BUFSIZE 32
char strbuf[BUFSIZE];

typedef enum {
    INIT = 0,
    READY = 1,
    GPS_READ = 2,
    GPS_ERROR = 3
} module_state_t;
module_state_t gps_state = INIT;

typedef struct {
    uint32_t time;
    uint8_t date;
    float_t latitude;
    char north_south;
    float_t longitude;
    char east_west;
    uint8_t num_sats;
    uint8_t sat_id;
    uint8_t sat_elev;
    uint8_t sat_azimuth;
    float_t ground_speed;
    float_t ground_course;
} gps_data;


// LCD Page Selection
typedef enum{
    PAGE_SPEED = 0,
    PAGE_LOCATION = 1,
    PAGE_TIME = 2
} page_t;

// Current LCD Page
volatile page_t current_page = PAGE_SPEED;

// Debounce for buttons
#define BUTTON_DEBOUNCE_US 150000
volatile uint64_t last_button_time_us = 0;

/*Prevent Implicit Declarations*/
void gps_periodic_irq();

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
// Parameters: x, y = position of top-left corner of the label box, speed_value = speed number to display
void display_speed(uint16_t x, uint16_t y, float speed_value) {
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t blue_color = RGB565(0, 0, 255);
    
    // Draw blue box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, blue_color);
    
    // Print "Speed:" label in the blue box
    tft_print_string(x + 10, y + 8, "Speed:", RGB565(255, 255, 255), blue_color);
    
    // Format and print the speed value below the box
    char speed_buffer[32];
    snprintf(speed_buffer, sizeof(speed_buffer), "%.1f mph", speed_value);
    tft_print_string(x + 10, y + label_box_height + 10, speed_buffer, 
                     RGB565(0, 0, 0), RGB565(255, 255, 255));
}

// Display Location: [lat, lon] in a red box
// Parameters: x, y = position of top-left corner of the label box, lat = latitude, lon = longitude
void display_location(uint16_t x, uint16_t y, float lat, float lon) {
    uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t red_color = RGB565(255, 0, 0);
    
    // Draw red box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, red_color);
    
    // Print "Location:" label in the red box
    tft_print_string(x + 10, y + 8, "Location:", RGB565(255, 255, 255), red_color);
    
    // Format and print coordinates below the box
    char location_buffer[64];
    snprintf(location_buffer, sizeof(location_buffer), "Lat: %.6f", lat);
    tft_print_string(x + 10, y + label_box_height + 10, location_buffer, 
                     RGB565(0, 0, 0), RGB565(255, 255, 255));
    
    snprintf(location_buffer, sizeof(location_buffer), "Lon: %.6f", lon);
    tft_print_string(x + 10, y + label_box_height + 10 + line_height, location_buffer, 
                     RGB565(0, 0, 0), RGB565(255, 255, 255));
}

// Display Time: [time_string] in a green box
// Parameters: x, y = position of top-left corner of the label box, time_str = time string to display
void display_time(uint16_t x, uint16_t y, const char* time_str) {
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t green_color = RGB565(0, 128, 0);
    
    // Draw green box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, green_color);
    
    // Print "Time:" label in the green box
    tft_print_string(x + 10, y + 8, "Time:", RGB565(255, 255, 255), green_color);
    
    // Print time string below the box
    tft_print_string(x + 10, y + label_box_height + 10, time_str, 
                     RGB565(0, 0, 0), RGB565(255, 255, 255));
}

void display_all(float speed_value, float lat, float lon, const char* time_str){
    display_speed(10, 10, speed_value);
    display_location(10, 80, lat, lon);
    display_time(10, 180, time_str);
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

void page_sel_isr(uint gpio, uint32_t events) {
   /*Set up code + global to change page state with different variables displayed*/
    uint64_t now = time_us_64();
    if (now - last_button_time_us < BUTTON_DEBOUNCE_US) {
        return; // debounce
    }
    last_button_time_us = now;

    if (gpio == (uint)button_1) {
        // Next page: SPEED -> LOCATION -> TIME -> SPEED
        current_page = (current_page + 1) % 3;
    } else if (gpio == (uint)button_2) {
        // Previous page
        current_page = (current_page + 3 - 1) % 3;
    }
}

// Init all GPIO pins for page selection buttons 
void page_sel_irq() {
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
    timer0_hw->intr = 1u << 1;
    last_set_time = timer0_hw->timerawl;
    gps_periodic_irq();
    timer0_hw->alarm[0] = timer0_hw->timerawl + 2500;

    // fill in the code here to send ALL startup functions to the GPS
}

void init_startup_timer() {
    /*Setting up a timer, it wont be the exact same but it should be similar for startup stuff*/
    timer0_hw->alarm[0] = 1E6;
    irq_set_exclusive_handler(TIMER0_IRQ_0, timer_isr);
    timer0_hw->inte = 1u << 0;
    irq_set_enabled(TIMER0_IRQ_0, true);
}

void init_lcd_disp_dma() {
    /*Once again, not exactly the same as this but if all values here are corrected it will work*/
    uint32_t temp = 0;
    //dma_hw->ch[1].read_addr = &uart1_hw->dr;
    //dma_hw->ch[1].write_addr = &spi1_hw->dr;
    dma_hw->ch[1].transfer_count = (8 | 0xf << 28);
    temp |= (1u << 2);
    temp |= (1u << 4);
    temp |= (4u << 8);
    temp |= (26u << 17);
    temp |= (1u << 0);
    dma_hw->ch[1].ctrl_trig = temp;
}

void init_uart_gps() {
    uart_init(uart1, 9600);
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(uart1, 0)); // TODO: double check naming of TX and RX PINS
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(uart1, 1)); // TODO: double check naming of TX and RX PINS
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    sleep_ms(1);
    uart_write_blocking(uart1, (const uint8_t*) "$PMTK104*37\r\n", strlen("$PMTK104*37\r\n"));
    uart_write_blocking(uart1,(const uint8_t*) "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C\r\n", strlen("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C<CR><LF>"));
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
    char buf[BUFSIZE];
    uart_read_blocking(uart1, (uint8_t*)buf, sizeof(buf) - 1);   
    buf[sizeof(buf) - 1] = '\0';
    tft_print_multiline(10, 20, buf, 
                         RGB565(255, 255, 255), RGB565(255, 0, 0), line_height);
    printf("%s",buf);

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
    page_sel_irq();

    tft_fill_screen(RGB565(255, 0, 0));

    for(;;);
    return 0;
}