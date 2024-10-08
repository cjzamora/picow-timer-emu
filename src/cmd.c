#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "cmd.h"
#include "clock.h"

/**
 * Command repeating timer
 * 
 * @var repeating_timer
 */
struct repeating_timer cmd_timer;

/**
 * Command data type
 * 
 * @var cmd_data_t
 */
typedef struct {
    void (*cmd_execute)(char *cmd);
} cmd_data_t;

/**
 * Command data
 * 
 * @var cmd_data_t
 */
cmd_data_t *cmd_data;

/**
 * Boot message
 * 
 * @return void
 */
void cmd_boot_message()
{
    printf("\033[2J\033[1;1H");
    printf("\033[1mPico Clock/Timer Emulator\033[0m\n");

    cmd_info();

    printf("Type '?' for help\n\n");
}

/**
 * Command info
 * 
 * @return void
 */
void cmd_info()
{
    u_int32_t sys_clk = clock_get_sys_freq_hz();
    u_int32_t out_clk = clock_get_freq_hz();
    u_int16_t pwm_div = clock_get_pwm_div();
    u_int16_t pwm_wrap = clock_get_pwm_wrap();
    u_int16_t duty_cycle = clock_get_duty_cycle();
    u_int8_t timer_type = clock_get_timer_type();
    u_int8_t mode = clock_get_mode();

    // determine timer type
    char timer_type_str[4];
    if (timer_type == CLOCK_TIMER_PWM) {
        strcpy(timer_type_str, "PWM");
    } else {
        strcpy(timer_type_str, "RPT");
    }   

    // determine mode
    char mode_str[16];
    if (mode == CLOCK_MONOSTABLE) {
        strcpy(mode_str, "Monostable");
    } else {
        strcpy(mode_str, "Astable");
    }

    printf(
        "\n"
        "Sys Clock:\t\t%luHz\n"
        "Out Clock:\t\t%luHz\n"
        "Mode:\t\t\t%s\n"
        "Timer:\t\t\t%s\n",
        sys_clk,
        out_clk,
        mode_str,
        timer_type_str
    );

    if (timer_type) {
        printf(
            "Divider:\t\t%d\n"
            "Wrap:\t\t\t%d\n",
            pwm_div,
            pwm_wrap
        );
    }

    printf("Duty Cycle:\t\t%d%%\n", duty_cycle);
    printf("\n");
}

/**
 * Command help
 * 
 * @return void
 */
void cmd_help()
{
    printf(
        "\n"
        "?\t\tshows this help\n"
        "start\t\tstarts the clock timer\n"
        "stop\t\tstops the clock timer\n"
        "step\t\tsteps the clock timer\n"
        "freq <hz>\tsets the clock frequency\n"
        "duty <percent>\tsets the clock duty cycle\n"
        "reset\t\tresets the clock timer\n"
        "reboot\t\treboots the pico to BOOTSEL mode\n"
        "clear\t\tclears the screen\n"
        "\n"
    );
}

/**
 * Command flush
 * 
 * @return void
 */
void cmd_flush() {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT && ch != '\n' && ch != EOF) {}
}

/**
 * Command execute
 * 
 * @param char *cmd
 * @return void
 */
void cmd_execute(char *cmd)
{
    // stop command timer
    cmd_stop();

    char *step_message = "* Monostable mode press `enter` to step and type `exit` and hit enter to go back to Astable mode\n";

    // help command
    if (strcmp(cmd, "?") == 0) {
        cmd_help();

    // start command
    } else if (strcmp(cmd, "start") == 0) {
        printf("* Clock started\n");
        clock_pulse_start();

    // stop command
    } else if (strcmp(cmd, "stop") == 0) {
        printf("* Clock stopped\n");
        clock_pulse_stop();

    // step command
    } else if (strcmp(cmd, "step") == 0) {
        clock_step(true);
        printf(step_message);
        cmd_info();

    // freq command
    } else if (strncmp(cmd, "freq", 4) == 0) {
        char *freq = cmd + 5;
        u_int32_t hz = atoi(freq);

        // limit frequency to 125MHz
        if (hz > 125000000) {
            printf("Frequency cannot be greater than 125000000\n");
        } else {
            clock_set_freq_hz(hz);
            cmd_info();
        }

    // duty command
    } else if (strncmp(cmd, "duty", 4) == 0) {
        char *duty = cmd + 5;
        u_int16_t duty_cycle = atoi(duty);
        
        // duty cycle can only be set in PWM mode
        if (clock_get_timer_type() == CLOCK_TIMER_RPT) {
            printf("Duty cycle can only be set in PWM mode\n");

        // duty cycle cannot be greater than 100
        } else if (duty_cycle > 100) {
            printf("Duty cycle cannot be greater than 100\n");
        } else {
            clock_set_duty_cycle(duty_cycle);
            cmd_info();
        }

    // reset command
    } else if (strcmp(cmd, "reset") == 0) {
        clock_reset();
        cmd_boot_message();

    // reboot command
    } else if (strcmp(cmd, "reboot") == 0) {
        printf("* Rebooting to BOOTSEL mode\n");
        reset_usb_boot(0, 0);

    // clear command
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_boot_message();

    // exit step mode command
    } else if (strcmp(cmd, "exit") == 0 && clock_get_mode() == CLOCK_MONOSTABLE) {
        clock_step(false);
        cmd_info();
    } else {
        // if in step mode
        if (clock_get_mode() == CLOCK_MONOSTABLE) {
            printf("...\n");
            clock_step_pulse();
        } else {
            printf("Unknown command\n");
        }
    }
    
    // run command timer
    cmd_run();
}

/**
 * Command timer callback
 * 
 * @param repeating_timer_t *t
 * @return bool
 */
bool cmd_timer_callback(repeating_timer_t *t)
{
    // get cmd data
    cmd_data_t *cmd_data = (cmd_data_t *) t->user_data;

    // get character
    int ch = getchar_timeout_us(0);
    // command buffer
    static char cmd_buffer[256];
    // command buffer index
    static unsigned int index = 0;

    // if no character
    if (ch == PICO_ERROR_TIMEOUT) {
        return true;
    }

    // if backspace
    if ((ch == 0x08 || ch == 0x7F)) {
        // if index is 0, do nothing
        if (index == 0) {
            printf(" ");
            return true;
        }

        cmd_buffer[--index] = 0;
        printf(" \b \b");

        return true;
    }

    // if character is newline or carriage return
    if ((ch == '\n' || ch == '\r')) {
        // execute command
        cmd_data->cmd_execute(cmd_buffer);
        // flush command buffer
        memset(cmd_buffer, 0, sizeof(char) * 256);
        // reset index
        index = 0;
    } else if (index < sizeof(cmd_buffer) - 1) {
        // add character to command buffer
        cmd_buffer[index++] = ch;
    }

    return true;
}

/**
 * Command stop timer
 * 
 * @return void
 */
void cmd_stop()
{
    cancel_repeating_timer(&cmd_timer);
    free(cmd_data);
}

/**
 * Command run timer
 * 
 * @return void
 */
void cmd_run()
{
    printf(">>> ");

    // create cmd data
    cmd_data = (cmd_data_t *) malloc(sizeof(cmd_data_t));
    // set cmd execute
    cmd_data->cmd_execute = cmd_execute;

    // flush input buffer
    cmd_flush();
    // start repeating timer
    add_repeating_timer_ms(50, cmd_timer_callback, cmd_data, &cmd_timer);
}

/**
 * Command init function
 * 
 * @return void
 */
void cmd_init()
{
    cmd_boot_message();
    cmd_run();
}