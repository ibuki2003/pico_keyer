#include <ctype.h>
#include <pico/platform.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>
#include <hardware/gpio.h>

#include "morse.h"

#define PIN 25

volatile char buf[256] = {0};
volatile uint8_t buf_head = 0;
volatile uint8_t buf_tail = 0;

volatile char cbuf[16];
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
            gpio_put(PIN, 0);
            running = false;
            return 0;
        } else {
            {
                uint8_t b = buf_tail;
                while (b != buf_head) {
                    printf("%02x ", buf[b]);
                    b = (b + 1) % 256;
                }
                printf("\n");
            }
            char c = buf[buf_tail];
            buf_tail = (buf_tail + 1) % 256;
            printf("pop char: %02x\n", c);
            if (c == ' ') {
                cbuf[cbuf_head] = 0x04;
                cbuf_head = (cbuf_head + 1) % 16;
            } else if (c & 0x80) { // special chars
                cbuf[cbuf_head] =
                    (c == RAW_DASH_CHAR)  ? 0xf3 :
                    (c == RAW_DOT_CHAR)   ? 0xf1 :
                    (c == RAW_SPACE_CHAR) ? 0x02 : 0x00;
                cbuf_head = (cbuf_head + 1) % 16;

                cbuf[cbuf_head] = 0x01;
                cbuf_head = (cbuf_head + 1) % 16;
            } else {
                morse m = MORSE_TABLE[c];
                uint8_t i = 1u << (m.len - 1);
                while (i) {
                    cbuf[cbuf_head] = (m.code & i) ? 0xf3 : 0xf1;
                    cbuf_head = (cbuf_head + 1) % 16;

                    cbuf[cbuf_head] = 0x01;
                    cbuf_head = (cbuf_head + 1) % 16;

                    i >>= 1;
                }
                cbuf[(cbuf_head + 15) % 16] = 0x03;
            }
        }
    }

    gpio_put(PIN, cbuf[cbuf_tail] & 0x80);
    uint8_t delay = cbuf[cbuf_tail] & 0x0f;
    cbuf_tail = (cbuf_tail + 1) % 16;

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

    gpio_init(PIN);
    gpio_set_dir(PIN, GPIO_OUT);

    alarm_id_t id = 0;

    enum CommandMode command_mode = false;

    uint16_t speed_buf = 0;

    while (1) {
        uint32_t c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            printf("char: 0x%02x\n", c);
            printf("mode: %d\n", command_mode);

            if (command_mode) {
                switch (command_mode) {
                    case CMD_INIT:
                        command_mode = CMD_OFF;
                        switch (c) {
                            case 'S': command_mode = CMD_SPEED; speed_buf = 0; break;

                            case '-': // raw dash
                                buf[buf_head] = '-' ^ (char)0x80;
                                buf_head = (buf_head + 1) % 256;
                                break;

                            case '.': // raw dot
                                buf[buf_head] = '.' ^ (char)0x80;
                                buf_head = (buf_head + 1) % 256;
                                break;

                            case ' ': // raw space
                                buf[buf_head] = ' ' ^ (char)0x80;
                                buf_head = (buf_head + 1) % 256;
                                break;

                            case '1': // raw on
                                buf_tail = buf_head;
                                cbuf_tail = cbuf_head;
                                cancel_alarm(id);
                                gpio_put(PIN, 1);
                                break;

                            case '0': // raw off
                                buf_tail = buf_head;
                                cbuf_tail = cbuf_head;
                                cancel_alarm(id);
                                gpio_put(PIN, 0);
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
                        gpio_put(PIN, 0);
                        break;

                    case 0x7f: // backspace
                        if (buf_head != buf_tail) {
                            buf_head = (buf_head + 255) % 256;
                            buf[buf_head] = 0;
                        } else if (cbuf_head != cbuf_tail) {
                            cbuf_tail = cbuf_head;
                            cancel_alarm(id);
                            gpio_put(PIN, 0);
                        }

                        break;

                    default:
                        buf[buf_head] = toupper(c);
                        buf_head = (buf_head + 1) % 256;
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
