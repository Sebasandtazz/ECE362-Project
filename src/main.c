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
/*Hardware mtk3339 Headers*/
//#include "gpsdata.h"
//////////////////////////////////////////////////////////////////////////////
#define BUFSIZE 32
char strbuf[BUFSIZE];

/*Prevent Implicit Declarations*/
void gps_periodic_irq();

/*Init of all of the pins used */
const int button_1 = 21;
const int button_2 = 26; 
const int led_1 = -1;
const int led_2 = -1;
const int UART_TX_PIN = 8;
const int UART_RX_PIN = 9;


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

void page_sel_isr() {
   /*Set up code + global to change page state with different variables displayed*/
    
}

void page_sel_irq() {
    /*Init all gpio pins for page_sel and led*/
}

uint32_t last_set_time = 0;

void timer_isr() {
    /*Setting up timer leaving my code here for reference*/
    timer0_hw->intr = 1u << 1;
    last_set_time = timer0_hw->timerawl;
    gps_periodic_irq();
    timer0_hw->alarm[0] = timer0_hw->timerawl + 25000;

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

    printf("%s",buf);

}


//////////////////////////////////////////////////////////////////////////////


int main()
{
    /*Call all inits here*/
    stdio_init_all();
    init_uart_gps();
    init_startup_timer();

    for(;;) {
       char buf[BUFSIZE];
        uart_read_blocking(uart1, (uint8_t*)buf, sizeof(buf) - 1);   
        buf[sizeof(buf) - 1] = '\0';
        printf(buf);
    }

    for(;;);
    return 0;
}