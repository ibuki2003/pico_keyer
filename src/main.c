#include <ctype.h>
#include <pico/platform.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>
#include <hardware/gpio.h>

#include "morse.h"

#define LED_PIN 25
#define PIN 0
#define GPIO_MASK ((1u << LED_PIN) | (1u << PIN))

#define BUF_SIZE 256
volatile char buf[BUF_SIZE] = {0};
volatile uint8_t buf_head = 0;
volatile uint8_t buf_tail = 0;

#define CBUF_SIZE 16
volatile char cbuf[CBUF_SIZE];
volatile uint8_t cbuf_head = 0;
volatile uint8_t cbuf_tail = 0;

volatile bool running = false;

uint16_t speed = 20;
uint32_t speed_delay = 60000; // us

#define RAW_DASH_CHAR  ('-' ^ (char)0x80)
#define RAW_DOT_CHAR   ('.' ^ (char)0x80)
#define RAW_SPACE_CHAR (' ' ^ (char)0x80)

int64_t timer_callback(alarm_id_t id, void *user_data) {
    if (cbuf_head == cbuf_tail) {
        if (buf_head == buf_tail) {
            // turn off for safe
            gpio_clr_mask(GPIO_MASK);
            running = false;
            return 0;
        } else {
            {
                uint8_t b = buf_tail;
                while (b != buf_head) {
                    printf("%02x ", buf[b]);
                    b = (b + 1) % BUF_SIZE;
                }
                printf("\n");
            }
            char c = buf[buf_tail];
            buf_tail = (buf_tail + 1) % BUF_SIZE;
            if (c == ' ') {
                cbuf[cbuf_head] = 0x04;
                cbuf_head = (cbuf_head + 1) % CBUF_SIZE;
            } else if (c & 0x80) { // special chars
                cbuf[cbuf_head] =
                    (c == RAW_DASH_CHAR)  ? 0xf3 :
                    (c == RAW_DOT_CHAR)   ? 0xf1 :
                    (c == RAW_SPACE_CHAR) ? 0x02 : 0x00;
                cbuf_head = (cbuf_head + 1) % CBUF_SIZE;

                cbuf[cbuf_head] = 0x01;
                cbuf_head = (cbuf_head + 1) % CBUF_SIZE;
            } else {
                morse m = MORSE_TABLE[c];
                uint8_t i = 1u << (m.len - 1);
                while (i) {
                    cbuf[cbuf_head] = (m.code & i) ? 0xf3 : 0xf1;
                    cbuf_head = (cbuf_head + 1) % CBUF_SIZE;

                    cbuf[cbuf_head] = 0x01;
                    cbuf_head = (cbuf_head + 1) % CBUF_SIZE;

                    i >>= 1;
                }
                cbuf[(cbuf_head + CBUF_SIZE - 1) % CBUF_SIZE] = 0x03;
            }
        }
    }

    if (cbuf[cbuf_tail] & 0x80) {
        gpio_set_mask(GPIO_MASK);
    } else {
        gpio_clr_mask(GPIO_MASK);
    }

    uint8_t delay = cbuf[cbuf_tail] & 0x0f;
    cbuf_tail = (cbuf_tail + 1) % CBUF_SIZE;

    return -(int64_t)(speed_delay * delay);
}

enum CommandMode {
    CMD_OFF,
    CMD_INIT,
    CMD_SPEED,
};


int main() {
    stdio_init_all();
    printf("Hello, world!\n");

    gpio_init_mask(GPIO_MASK);
    gpio_set_dir_out_masked(GPIO_MASK);

    alarm_id_t id = 0;

    enum CommandMode command_mode = false;

    uint16_t speed_buf = 0;

    while (1) {
        uint32_t c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            printf("char: 0x%02x\n", c);

            if (command_mode) {
                switch (command_mode) {
                    case CMD_INIT:
                        command_mode = CMD_OFF;
                        switch (c) {
                            case 'S': command_mode = CMD_SPEED; speed_buf = 0; break;

                            case '-': // raw dash
                                buf[buf_head] = '-' ^ (char)0x80;
                                buf_head = (buf_head + 1) % BUF_SIZE;
                                break;

                            case '.': // raw dot
                                buf[buf_head] = '.' ^ (char)0x80;
                                buf_head = (buf_head + 1) % BUF_SIZE;
                                break;

                            case ' ': // raw space
                                buf[buf_head] = ' ' ^ (char)0x80;
                                buf_head = (buf_head + 1) % BUF_SIZE;
                                break;

                            case '1': // raw on
                                buf_tail = buf_head;
                                cbuf_tail = cbuf_head;
                                cancel_alarm(id);
                                gpio_set_mask(GPIO_MASK);
                                break;

                            case '0': // raw off
                                buf_tail = buf_head;
                                cbuf_tail = cbuf_head;
                                cancel_alarm(id);
                                gpio_clr_mask(GPIO_MASK);
                                break;
                        }
                        break;

                    case CMD_SPEED:
                        switch (c) {
                            case '0' ... '9':
                                speed_buf = speed_buf * 10 + (c - '0');
                                break;
                            case '\n':
                            case '\r':
                                speed = speed_buf;
                                speed_delay = 1200000 / speed;
                                printf("speed: %d WPM\n", speed);
                                command_mode = CMD_OFF;
                                break;

                            default: command_mode = CMD_OFF; break;
                        }
                        break;
                }
            } else {
                switch (c) {
                    case '\n':
                    case '\r':
                        break;

                    case '\\':
                       command_mode = CMD_INIT;
                       break;

                    case 0x15: // ctrl-u
                        buf_head = buf_tail;
                        cbuf_head = cbuf_tail;
                        cancel_alarm(id);
                        gpio_clr_mask(GPIO_MASK);
                        break;

                    case 0x7f: // backspace
                        if (buf_head != buf_tail) {
                            buf_head = (buf_head + BUF_SIZE - 1) % BUF_SIZE;
                            buf[buf_head] = 0;
                        } else if (cbuf_head != cbuf_tail) {
                            cbuf_tail = cbuf_head;
                            cancel_alarm(id);
                            gpio_clr_mask(GPIO_MASK);
                        }

                        break;

                    default:
                        buf[buf_head] = toupper(c);
                        buf_head = (buf_head + 1) % BUF_SIZE;
                        break;
                }
            }
            if (!running && buf_head != buf_tail) {
                running = true;
                add_alarm_in_ms(1, timer_callback, NULL, false);
            }
        }

    }
    return 0;
}
