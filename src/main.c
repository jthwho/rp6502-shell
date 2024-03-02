/**
 * @file      main.c
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define CMD_BUF_MAX 511
#define CMD_TOKEN_MAX 64

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

#define CHAR_BELL   0x07
#define CHAR_BS     0x08
#define CHAR_CR     0x0D
#define CHAR_LF     0x0A
#define NEWLINE     "\r\n"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static const char hexdigits[] = "0123456789ABCDEF";

typedef struct {
    int bytes;
    char buffer[CMD_BUF_MAX+1];
} cmdline_t;

typedef struct {
    const char *cmd;
    const char *help;
    int (*func)(int argc, char **argv);
} cmd_t;

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

int cmd_help(int, char **);
//int cmd_token(int, char **);
int cmd_xr(int, char **);
int cmd_mr(int, char **);

static const cmd_t commands[] = {
    { "help",   "print a list of commands", cmd_help },
    //{ "token",  "tests the tokenization", cmd_token },
    { "xr",     "reads xram", cmd_xr },
    { "mr",     "reads ram", cmd_mr }
};

inline void tx_char(char c) {
    TX_READY_SPIN;
    RIA.tx = c;
    return;
}

void tx_chars(const char *buf, int ct) {
    for(; ct; ct--, buf++) tx_char(*buf);
    return;
}

void tx_string(const char *buf) {
    while(*buf) tx_char(*buf++);
    return;
}

void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    RIA.step0 = 1;
    RIA.addr0 = addr;
    for(; size; size--) *buf++ = RIA.rw0;
    return;
}

void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    uint8_t *data = (uint8_t *)addr;
    for(; size; size--) *buf++ = *data++;
    return;
}

// Assumes str points to at least two bytes.
int hexstr(char *str, uint8_t val) {
    str[0] = hexdigits[val >> 4];
    str[1] = hexdigits[val & 0xF];
    return 2;
}

#define HEXDUMP_LINE_SIZE 16
void hexdump(uint16_t addr, uint16_t bytes, char_stream_func_t streamer, read_data_func_t reader) {
    int i;
    uint8_t data[HEXDUMP_LINE_SIZE];
    char string[HEXDUMP_LINE_SIZE * 3 + 32];

    while(bytes) {
        char *str = string;
        int rd = bytes > sizeof(data) ? sizeof(data) : bytes;
        str += hexstr(str, addr >> 8);
        str += hexstr(str, addr & 0xFF);
        *str++ = ':';
        reader(data, addr, rd);
        for(i = 0; i < rd; i++) {
            *str++ = ' ';
            str += hexstr(str, data[i]);
        }
        *str++ = ' ';
        for(i = 0; i < rd; i++) {
            char b = (data[i] >= 32 && data[i] <= 126) ? data[i] : '.';
            *str++ = b;
        }
        *str++ = CHAR_CR;
        *str++ = CHAR_LF;
        streamer(string, str - string);
        bytes -= rd;
        addr += rd;
    }
    return;
}

void prompt() {
    fflush(stdout);  // FIXME: This doesn't seem to work.
    tx_string("> ");
    return;
}

// Reformats the given buffer into tokens, delimited by spaces.  It places those
// tokens in the tokenList array (up to maxTokens).  The given buffer may be
// reformatted as part of this function so you'll want to make a copy of it 
// if you need the original version after this function.  Here are the rules
// it follows:
// - Leading and trailing spaces are ignored, a token may only star with a non
//   space unless a space is escaped (i.e. "\ token" will be tokenized as " token")
// - Additional spaces after the first space will be ignored (i.e. won't create empty tokens)
// - The backslash character causes the following character to be added to the token
//   regardless
// - The " or ' character will cause a quote to start and all characters until the
//   quote of the same type will be placed in a single token.  The backslash escape
//   is still followed, so you can add a quote character w/o exiting a quote block
//   by prefacing it with \ (e.g. "this is a \"test\"" will be interpreted as 
//   'this is a "test"')
int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens) {
    char *in = buf;
    char *out = buf;
    char *max = buf + maxBuf;
    bool escape = false;
    char quote = 0;
    int tokens = 0;
    int tokenLen = 0;

    while(in != max) {
        char c = *in;
        if(!c) break;
        in++;
        if(!escape) {
            // Check for an backslash escape code.
            if(c == '\\') {
                escape = true;
                continue;
            }
            if(quote) {
                // If we're in a quote, only a quote of the same type as the start
                // can exit the quote
                if(c == quote) {
                    quote = 0;
                    continue;
                }
            } else {
                // Not in a quote, so spaces are token delimiters
                if(c == ' ') {
                    if(!tokenLen) continue; // No token yet, ignore
                    *out++ = 0;
                    tokenLen = 0;
                    tokens++;
                    continue;
                // Check for a start of a quote and record the type
                } else if(c == '\'' || c == '"') {
                    quote = c;
                    continue;
                }
            }
        }

        // Ok, got a character we should put in a token.
        if(!tokenLen) {
            if(tokens == maxTokens) break; // No more space for tokens.
            tokenList[tokens] = out;
        }
        *out++ = c;
        tokenLen++;
        escape = false; // Just set escape to false because a normal character will always end an escape
    }
    // Make sure we account for the current token in progress (if there is one)
    if(tokenLen) {
        *out = 0;
        tokens++;
    }
    // FIXME: We don't really have the concept of an error.  We could leave with a quote or escape
    // still in progress.  Probably should have a way to message these error conditions.
    return tokens;
}

int execute(cmdline_t *cl) {
    int i;
    char *tokenList[64];
    int tokens = tokenize(cl->buffer, cl->bytes, tokenList, ARRAY_SIZE(tokenList));
    if(!tokens) return 0;
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        if(!strcmp(tokenList[0], commands[i].cmd)) {
            return commands[i].func(tokens, tokenList);
        }
    }
    tx_string("Unknown command" NEWLINE);
    return -1;
}

int main(void) {
    char last_rx = 0;
    static cmdline_t cmdline = {0};
    tx_string(NEWLINE "rp6502 shell" NEWLINE);
    prompt();
    while(1) {
        if(RX_READY) {
            char rx = (char)RIA.rx;
            // Normal character, just put it on the pile.
            if(rx >= 32 && rx <= 126) {
                if(cmdline.bytes == CMD_BUF_MAX) {
                    tx_char(0x7); // if the buffer is full, send a bell
                } else {
                    cmdline.buffer[cmdline.bytes++] = rx;
                    tx_char(rx);
                }
            // Backspace
            } else if(rx == CHAR_BS) {
                if(cmdline.bytes) {
                    cmdline.bytes--;
                    tx_char(CHAR_BS);
                    tx_char(' ');
                    tx_char(CHAR_BS);
                } else {
                    tx_char(CHAR_BELL);
                }
            // Enter
            } else if(rx == CHAR_CR || rx == CHAR_LF) {
                if(rx == CHAR_LF && last_rx == CHAR_CR) continue; // Ignore CRLF
                tx_string(NEWLINE);
                execute(&cmdline);
                cmdline.bytes = 0;
                prompt();
            }
            last_rx = rx; // Last line in RX_READY
        }
    }
    return 0;
}

int cmd_help(int, char **) {
    int i;
    tx_string("Commands: " NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        tx_string(commands[i].cmd);
        tx_string(" - ");
        tx_string(commands[i].help);
        tx_string(NEWLINE);
    }
    return 0;
}

/*
int cmd_token(int argc, char **argv) {
    int i;
    for(i = 0; i < argc; i++) {
        printf("%d: [%s]" NEWLINE, i, argv[i]);
    }
    return 0;
}
*/

int cmd_xr(int argc, char **argv) {
    uint16_t addr;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: xr addr [bytes]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    hexdump(addr, size, tx_chars, xram_reader);
    return 0;
}

int cmd_mr(int argc, char **argv) {
    uint16_t addr;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: mr addr [bytes]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    hexdump(addr, size, tx_chars, ram_reader);
    return 0;
}


