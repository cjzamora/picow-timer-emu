#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "pico/multicore.h"

// define modes
#define ASTABLE   0
#define MONOSTABLE 1
// debounce delay in ms for button press
#define DEBOUNCE_DELAY 200

// ADC0 pin for potentiometer
const uint POTENTIOMETER_PIN = 26;

// GPIO pin for MODE button
const uint MODE_PIN = 14;
// GPIO pin for STEP button
const uint STEP_PIN = 15;
// GPIO pin for PULSE output (led)
const uint PULSE_PIN = 16;
// GPIO pin for CLOCK output
const uint CLOCK_PIN = 28;

// define frequency in Hz
volatile int frequency = 1;
// define duty cycle in %
volatile int duty_cycle = 50;

// volatile variables for interrupt handling
volatile int mode = ASTABLE;
volatile int pulse = true;
volatile uint32_t last_interrupt_ms = 0;

/**
 * Interrupt handler for button press
 * 
 * @param gpio GPIO pin number
 * @param events GPIO event
 *
 * @return void
 */
void handle_button_interrupt(uint gpio, uint32_t events) 
{
    // get current time in ms
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // debounce button press
    if ((now - last_interrupt_ms) < DEBOUNCE_DELAY) {
        return;
    }

    // MODE button pressed
    if (gpio == MODE_PIN) {
        // toggle mode
        mode = mode == ASTABLE ? MONOSTABLE : ASTABLE;
        // determine pulse
        pulse = mode == MONOSTABLE ? false : true;
    } else if (gpio == STEP_PIN && mode == MONOSTABLE) {
        // STEP button pressed
        pulse = true;
    }

    // update last interrupt time
    last_interrupt_ms = now;
}

/**
 * Core 1 process for analog pin reading
 * 
 * @return void
 */
void start_adc() 
{
    // initialize adc
    adc_init();
    // init adc GPIO
    adc_gpio_init(POTENTIOMETER_PIN);
    // set adc channel
    adc_select_input(0);

    while(true) {
        // read adc value
        uint16_t raw = adc_read();

        // capture 64 samples to avoid noise
        int samples = 64;
        int sum = 0;
        for (int i = 0; i < samples; i++) {
            sum += adc_read();
            sleep_ms(10);
        }

        // calculate average
        sum = sum / samples;
        sum = (sum * 999 / 4095) + 1;

        // update frequency
        frequency = sum;

        sleep_ms(1000);
    }
}

int main() 
{
    // initialize stdio
    stdio_init_all();

    // initialize Wi-Fi
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed");
        return -1;
    }

    // init GPIOs
    gpio_init(MODE_PIN);
    gpio_init(STEP_PIN);
    gpio_init(PULSE_PIN);
    gpio_init(CLOCK_PIN);

    // set GPIO mode
    gpio_set_dir(MODE_PIN, GPIO_IN);
    gpio_set_dir(STEP_PIN, GPIO_IN);
    gpio_set_dir(PULSE_PIN, GPIO_OUT);
    gpio_set_dir(CLOCK_PIN, GPIO_OUT);

    // set GPIO pull-up
    gpio_pull_up(MODE_PIN);
    gpio_pull_up(STEP_PIN);

    // set GPIO IRQ
    gpio_set_irq_enabled_with_callback(MODE_PIN, GPIO_IRQ_EDGE_FALL, true, &handle_button_interrupt);
    gpio_set_irq_enabled_with_callback(STEP_PIN, GPIO_IRQ_EDGE_FALL, true, &handle_button_interrupt);

    // if CLOCK_FREQ is defined
    if (CLOCK_FREQ && CLOCK_FREQ > 0) {
        // set frequency
        frequency = CLOCK_FREQ;
    } else {
        // put analog pin reading on core 1
        multicore_launch_core1(start_adc);
    }

    // wait for core 1 to start
    sleep_ms(1000);

    while(true) {
        // if not pulse
        if (!pulse) {
            continue;
        }

        // calculate high/low time in ms
        int sleep_in_ms = 1000 / frequency;
        int high_time = sleep_in_ms * duty_cycle / 100;
        int low_time = sleep_in_ms - high_time;

        // if debug mode
        if (CLOCK_DEBUG) {
            printf(
                "Mode: %d, Freq: %dhz, Duty: %d, High: %d, Low: %d\n", 
                mode, 
                frequency, 
                duty_cycle, 
                high_time, 
                low_time
            ); 
        }

        // high
        gpio_put(CLOCK_PIN, 1);
        gpio_put(PULSE_PIN, 1);
        sleep_ms(high_time);

        // low
        gpio_put(PULSE_PIN, 0);
        gpio_put(CLOCK_PIN, 0);

        // low time for astable mode only
        if (mode == ASTABLE) {
            sleep_ms(low_time);
        } else {
            pulse = false;
        }
    }
}