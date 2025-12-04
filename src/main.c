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
#include "lcd.h"
//////////////////////////////////////////////////////////////////////////////
#define BUFSIZE 256
char strbuf[BUFSIZE];
#define PI 3.14159265358979323846

typedef struct {
    char ptmk[16];
    char time[16];
    char date[16];
    char latitude[16];
    char north_south[16];
    char longitude[16];
    char east_west[16];
    char fix[16];
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
static int duty_cycle = 0; 
static int dir = 0; 
static int color = 0;


// Current LCD Page
volatile page_t current_page = PAGE_SPEED;

/*Prevent Implicit Declarations*/
void gps_periodic_irq();
void disp_page();

/*Init of all of the pins used */
const int button_1 = 21;
const int button_2 = 26; 
const int led_1 = 22;
const int led_2 = 23;
const int led_3 = 24;
const int led_4 = 25;
const int UART_TX_PIN = 8;
const int UART_RX_PIN = 9;
const int SPI_SCK = 18;
const int SPI_CSn = 17;
const int SPI_TX = 19;
const int SPI_RX = 16;
const int DISP_DC = 20;
const int DISP_RST = 15;



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
    spi_init(spi0, 1250000000); // Initialize SPI0 at 10MHz
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
void page_sel_isr() {
   /*Set up code + global to change page state with different variables displayed*/
   if (gpio_get_irq_event_mask(button_2) == GPIO_IRQ_EDGE_RISE)
   {
        gpio_acknowledge_irq(button_2, GPIO_IRQ_EDGE_RISE);
        current_page = (current_page - 1) % 4;
        disp_page();
        printf("NEW PAGE SELECTED\n");
   }
   else
   {
        gpio_acknowledge_irq(button_1, GPIO_IRQ_EDGE_RISE);
        current_page = (current_page + 1) % 4;
        disp_page();
        printf("NEW PAGE SELECTED\n");
   }
   
}

// Init all GPIO pins for page selection buttons 
void page_sel_init() {
    gpio_init(button_2);
    gpio_init(button_1);

    gpio_add_raw_irq_handler_masked((1u << button_2 | 1u << button_1), page_sel_isr);
    gpio_set_irq_enabled(button_2,GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(button_1,GPIO_IRQ_EDGE_RISE, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

uint32_t last_set_time = 0;

void timer_isr() {
    /*Setting up timer leaving my code here for reference*/
    timer0_hw->intr = 1u << 0;
    last_set_time = timer0_hw->timerawl;
    gps_periodic_irq();
    timer0_hw->alarm[0] = timer0_hw->timerawl + 5000;

    // fill in the code here to send ALL startup functions to the GPS
}

void screen_isr() {
    /*Setting up timer leaving my code here for reference*/
    timer0_hw->intr = 1u << 1;
    last_set_time = timer0_hw->timerawl;
    //tft_fill_screen(RGB565(255,255,255));
    disp_page();
    //page_sel_isr();
    timer0_hw->alarm[1] = timer0_hw->timerawl + 2500000;   
}

void init_startup_timer() {
    /*Setting up a timer, it wont be the exact same but it should be similar for startup stuff*/
    timer0_hw->alarm[0] = 1E6;
    timer0_hw->alarm[1] = 15E5;
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
    char *tokens[20];
    const char delimiter[] = ",";
    tokens[0] = strtok(message, delimiter);
    while ((tokens[i] != NULL || i < 10))
    {
        i++;
        if (i >= 20) break;
        tokens[i] = strtok(NULL, delimiter);
    }
    
    // OK, I can't use strcomp in the case function as it isnt constant
    // I need to figure out a way that I can set this up to be a bitwise status int or smth. I think the solution is to have a
    // Set of ints above here that 

    if (tokens[0] && strcmp(tokens[0],"$GPRMC") == 0)
    {
        message_type = 1;
    }
    else if (tokens[0] && strcmp(tokens[0],"$GPVTG") == 0)
    {
        message_type = 2;
    }
    else if (tokens[0] && strcmp(tokens[0],"$GPGGA") == 0)
    {
        message_type = 3;
    }
    else
    {
        return;
    }
    if (tokens[0]) strcpy(gps.ptmk, tokens[0]);

    
    switch (message_type){
        case 1: // GPRMC
            if (tokens[7]) //strcpy(gps.ground_speed, tokens[7]);
            if (tokens[8]) strcpy(gps.ground_course, tokens[8]);
            break;

        case 2: // GPVTG
            if (tokens[6]) strcpy(gps.ground_speed, tokens[6]);
            if (tokens[1]) strcpy(gps.ground_course, tokens[1]);
            break;

        case 3: // GPGGA
            if (tokens[1]) strcpy(gps.time, tokens[1]);
            if (tokens[2]) strcpy(gps.latitude, tokens[2]); 
            if (tokens[3]) strcpy(gps.north_south, tokens[3]);
            if (tokens[4]) strcpy(gps.longitude, tokens[4]);
            if (tokens[5]) strcpy(gps.east_west, tokens[5]);
            if (tokens[6]) strcpy(gps.fix, tokens[6]);
            if (tokens[7]) strcpy(gps.num_sats, tokens[7]);
            break;

        default:
            break;
    }

}


void init_uart_gps() {
    uart_init(uart1, 9600);
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(uart1, 0)); // TODO: double check naming of TX and RX PINS
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(uart1, 1)); // TODO: double check naming of TX and RX PINS
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    sleep_ms(1);
    uart_write_blocking(uart1, (const uint8_t*) "$PMTK104*37\r\n", strlen("$PMTK104*37\r\n"));
    uart_write_blocking(uart1,(const uint8_t*) "$PMTK314,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C\r\n", strlen("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2C<CR><LF>"));
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

    char buf[BUFSIZE] = {0};
    while (uart_is_readable(uart1)) {
        uart_getc(uart1);
    }
    char curr;
    do {
        while (!uart_is_readable(uart1));  
        curr = uart_getc(uart1);
    } while (curr != '$');

    size_t i = 0;
    buf[i++] = '$';

    while (i < BUFSIZE - 1) {

        while (!uart_is_readable(uart1));  
        curr = uart_getc(uart1);

        if (curr == '\n' || curr == '\r')
            break;

        buf[i++] = curr;
    }

    buf[i] = '\0';

    //printf("%s\n", buf);
    gps_parser(buf);
    printf("Time: %s, Message Type:%s, Speed:%s, Longitude:%s, Fixed:%s Num Sats:%s\n", gps.time, gps.ptmk, gps.ground_speed, gps.longitude, gps.fix, gps.num_sats);
}

void disp_page(){
    tft_fill_screen(RGB565(255,255,255));
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
    gpio_set_function(led_1, GPIO_FUNC_PWM);
    gpio_set_function(led_2, GPIO_FUNC_PWM);
    gpio_set_function(led_3, GPIO_FUNC_PWM);
    gpio_set_function(led_4, GPIO_FUNC_PWM);


    uint slice_num_0 = pwm_gpio_to_slice_num(led_1);
    uint slice_num_1 = pwm_gpio_to_slice_num(led_3);
    

    // Same clock divider on all slices
    pwm_set_clkdiv(slice_num_0, 32.0f);
    pwm_set_clkdiv(slice_num_1, 32.0f);

    // Same period (TOP) on all slices
    pwm_set_wrap(slice_num_0, period - 1);
    pwm_set_wrap(slice_num_1, period - 1);

    // Initial duty on each pin (using correct channel for each GPIO)
    pwm_set_chan_level(slice_num_0, pwm_gpio_to_channel(led_1), duty_cycle);
    pwm_set_chan_level(slice_num_0, pwm_gpio_to_channel(led_2), duty_cycle);
    pwm_set_chan_level(slice_num_1, pwm_gpio_to_channel(led_3), duty_cycle);
    pwm_set_chan_level(slice_num_1, pwm_gpio_to_channel(led_4), duty_cycle);


    // Enable all slices
    pwm_set_enabled(slice_num_0, true);
    pwm_set_enabled(slice_num_1, true);
}

void pwm_breathing() {
    uint slice_num_0 = pwm_gpio_to_slice_num(led_1);
    uint slice_num_1 = pwm_gpio_to_slice_num(led_3);
    pwm_hw->intr = 1u << slice_num_0;

    float speed_setting = 0.0f;
    if (gps.ground_speed && gps.ground_speed[0] != '\0')
        speed_setting = strtof(gps.ground_speed, NULL);

    duty_cycle = speed_setting * 200;

    pwm_set_chan_level(slice_num_0, pwm_gpio_to_channel(led_1), duty_cycle);
    pwm_set_chan_level(slice_num_0, pwm_gpio_to_channel(led_2), duty_cycle);
    pwm_set_chan_level(slice_num_1, pwm_gpio_to_channel(led_3), duty_cycle);
    pwm_set_chan_level(slice_num_1, pwm_gpio_to_channel(led_4), duty_cycle);
}


void init_pwm_irq() {
    // Use the slice that drives PWM_PIN0 for the IRQ
    uint slice_num_0 = pwm_gpio_to_slice_num(led_1);
    uint slice_num_1 = pwm_gpio_to_slice_num(led_3);

    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pwm_breathing);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_set_irq0_enabled(slice_num_0, true);
    pwm_set_irq1_enabled(slice_num_1, true);

    uint current_period = pwm_hw->slice[slice_num_0].top;


    // Initialize all slices to full brightness at startup
    pwm_set_both_levels(slice_num_0, current_period, current_period);
    pwm_set_both_levels(slice_num_1, current_period, current_period);
    
}

//////////////////////////////////////////////////////////////////////////////


int main()
{
    /*Call all inits here*/
    stdio_init_all();
    init_uart_gps();
    page_sel_init();
    init_startup_timer();
    init_spi();
    init_disp();
    tft_init();
   

    uint32_t period = 100000;     // tune as desired
    uint32_t initial_dc = 0;    // start from 0% and let ISR drive it

    init_pwm_static(period, initial_dc);
    init_pwm_irq();
    // -------------------------------

    tft_fill_screen(RGB565(255, 255, 255));

    for(;;);
    return 0;
}