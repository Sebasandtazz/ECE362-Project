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


//////////////////////////////////////////////////////////////////////////////

/*Init of all of the pins used */
const int button_1 = 21;
const int button_2 = 26; 
const int led_1 = -1;
const int led_2 = -1;
const int UART_TX_PIN = -1;
const int UART_RX_PIN = -1;


typedef enum {
    INIT = 0,
    READY = 1,
    GPS_READ = 2,
    GPS_ERROR = 3
} module_state_t;
module_state_t gps_state = INIT;


void page_sel_isr() {
   /*Set up code + global to change page state with different variables displayed*/
    
}

void page_sel_irq() {
    /*Init all gpio pins for page_sel and led*/
}

uint32_t last_set_time = 0;

void timer_isr() {
    /*Setting up timer leaving my code here for reference*/
    // timer0_hw->intr = 1u << 0;
    // last_set_time = timer0_hw->timerawl;

}

void init_reaction_timer() {
    /*Setting up a timer, it wont be the exact same but it should be similar for startup stuff*/
    // timer0_hw->alarm[0] = 1E6;
    // irq_set_exclusive_handler(TIMER0_IRQ_0, timer_isr);
    // timer0_hw->inte = 1u << 0;
    // irq_set_enabled(TIMER0_IRQ_0, true);
}

void init_lcd_disp_dma() {
    /*Once again, not exactly the same as this but if all values here are corrected it will work*/
    // uint32_t temp = 0;
    // dma_hw->ch[1].read_addr = message;
    // dma_hw->ch[1].write_addr = &spi1_hw->dr;
    // dma_hw->ch[1].transfer_count = (8 | 0xf << 28);
    // temp |= (1u << 2);
    // temp |= (1u << 4);
    // temp |= (4u << 8);
    // temp |= (26u << 17);
    // temp |= (1u << 0);
    // dma_hw->ch[1].ctrl_trig = temp;
}

//////////////////////////////////////////////////////////////////////////////


int main()
{
    /*Call all inits here*/

    for(;;) {
       /*Put in any recurring things here, although they should be sparse as
       all active transfers after the intializations should be DMA controlled.
       This can serve to send status updates or something.*/
    }

    for(;;);
    return 0;
}