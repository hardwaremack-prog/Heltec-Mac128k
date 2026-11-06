/*
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef UMAC_H
#define UMAC_H

#include "disc.h"
#include "via.h"
#include "machw.h"

int     umac_init(void *_ram_base, void *_rom_base, disc_descr_t discs[DISC_NUM_DRIVES]);
int     umac_loop(void);
void    umac_reset(void);
void    umac_opt_disassemble(int enable);
void    umac_mouse(int deltax, int deltay, int button);
void    umac_kbd_event(uint8_t scancode, int down);
/* Heltec-Mac additions */
int     umac_kbd_ready(void);
void    umac_mouse_abs(int x, int y, int button);
int     umac_crashed(void);
int     umac_sound_frame(uint8_t samples[370], int *volume);
/* Real-time clock chip: Mac time = seconds since 1 Jan 1904, local time */
void     umac_rtc_set(uint32_t mac_seconds);
uint32_t umac_rtc_get(void);
uint8_t *umac_rtc_pram(void);            /* 20 bytes of parameter RAM */
int      umac_rtc_pram_dirty(void);      /* 1 if PRAM changed since the last call */
void     umac_set_time_now(uint32_t mac_seconds);
/* Printer port (serial port B): ready() == 0 makes the Mac wait; out() gets each byte */
void     umac_set_printer(int (*ready)(void), void (*out)(uint8_t b));

extern int umac_is_se;
void      adb_sixtieth(void);
static inline void      umac_vsync_event(void)
{
        if (umac_is_se)
                adb_sixtieth();         /* ADB: tell the Mac if a key or click is waiting */
        via_caX_event(2);
}

void     umac_rtc_tick(void);
static inline void      umac_1hz_event(void)
{
        umac_rtc_tick();
        via_caX_event(1);
}

/* Return the offset into RAM of the current display buffer */
static inline unsigned int      umac_get_fb_offset(void)
{
        /* FIXME: Implement VIA RA6/vid.pg2 */
        return RAM_SIZE - ((DISP_WIDTH * DISP_HEIGHT / 8) + 0x380);
}

#endif
