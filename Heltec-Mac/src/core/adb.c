/* Apple Desktop Bus for the Mac SE: the transceiver the ROM drives through the VIA, plus an
 * ADB keyboard (address 2) and mouse (address 3).
 *
 * The bus logic is a port of Mini vMac's ADBEMDEV.c / ADBSHARE.h,
 * Copyright (C) 2008 Paul C. Pratt, GNU GPL version 2.  Heltec-Mac changes: absolute mouse
 * (position goes straight to the Mac's cursor globals, so the mouse only reports buttons),
 * a small event queue, and timing in emulated microseconds.
 *
 * Wiring (Mac SE): VIA PB4/PB5 = bus state ST0/ST1 (from the Mac), PB3 = ADB interrupt
 * (to the Mac), the VIA shift register carries the bytes.
 */
#include <stdint.h>
#include <string.h>
#include "adb.h"

#ifdef MACMESH_HOST
#include <stdio.h>
#include <stdlib.h>
static int adb_trace = -1;
#define ATRACE(...) do { if (adb_trace < 0) adb_trace = getenv("ADBTRACE") != 0; if (adb_trace) fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define ATRACE(...) do {} while (0)
#endif

/* provided by via.c */
uint8_t via_sr_shift_out(void);        /* device reads the byte the Mac put in SR */
void    via_sr_rx(uint8_t val);         /* device sends a byte to the Mac */

#define ADB_RESPONSE_US 700             /* the transceiver answers ~0.7 ms after a state change */

static int      adb_int = 1;            /* PB3 level; Mini vMac's wires start at 1 */
static int      state_now = 3;
static int      pending_state = -1;
static uint64_t pending_due;

static uint8_t  cur_cmd;
static int      talk_buf, listen_buf;
static uint8_t  dat[8], sz_dat, idx_dat;
static uint8_t  mouse_addr = 3, kbd_addr = 2;
static uint8_t  not_so_rand = 1;
static int      mouse_button;

/* events from the browser, in order */
#define QN 64
enum { EV_KEY, EV_BUTTON };
static struct { uint8_t kind, val; } q[QN];
static volatile int qh, qt;

static int  q_empty(void) { return qh == qt; }
static int  q_kind(void) { return q[qt].kind; }
static void q_pop(void) { qt = (qt + 1) % QN; }

int     adb_room(void) { return (QN - 1 - (qh - qt + QN) % QN); }

void    adb_key(uint8_t code, int down)
{
        if (!adb_room()) return;
        q[qh].kind = EV_KEY; q[qh].val = (down ? 0x00 : 0x80) | (code & 0x7f);
        qh = (qh + 1) % QN;
}

void    adb_mouse_button(int down)
{
        if (!adb_room()) return;
        ATRACE("[ADB queue button %s]\n", down ? "DOWN" : "up");
        q[qh].kind = EV_BUTTON; q[qh].val = down ? 1 : 0;
        qh = (qh + 1) % QN;
}

void    adb_reset(void)
{
        adb_int = 1; state_now = 3; pending_state = -1;
        cur_cmd = 0; talk_buf = listen_buf = 0; sz_dat = idx_dat = 0;
        mouse_addr = 3; kbd_addr = 2; mouse_button = 0;
        qh = qt = 0;
}

int     adb_int_line(void) { return adb_int; }

/* ------------------------------------------------------------------ devices */

static void mouse_talk(void)
{
        switch (cur_cmd & 3) {
        case 0: {
                int change = 0;
                if (!q_empty() && q_kind() == EV_BUTTON) {
                        mouse_button = q[qt].val;
                        q_pop();
                        change = 1;
                }
                if (change) {
                        sz_dat = 2; talk_buf = 1;
                        dat[0] = mouse_button ? 0x00 : 0x80;   /* bit 7 clear = pressed; no movement */
                        ATRACE("[ADB mouse reports button %s, %d events left]\n", mouse_button ? "DOWN" : "up", (qh - qt + QN) % QN);
                        dat[1] = 0x80;
                }
                break;
        }
        case 3:
                sz_dat = 2; talk_buf = 1;
                dat[0] = 0x60 | (not_so_rand & 0x0f); dat[1] = 0x01;   /* handler 1 */
                not_so_rand++;
                break;
        }
}

static void mouse_listen(void)
{
        if ((cur_cmd & 3) == 3 && dat[1] == 0xFE) mouse_addr = dat[0] & 0x0f;
}

static int next_key(uint8_t *k)
{
        if (q_empty() || q_kind() != EV_KEY) return 0;
        *k = q[qt].val;
        q_pop();
        return 1;
}

static void kbd_talk(void)
{
        switch (cur_cmd & 3) {
        case 0: {
                uint8_t k;
                if (next_key(&k)) {
                        sz_dat = 2; talk_buf = 1;
                        dat[0] = k;
                        dat[1] = next_key(&k) ? k : 0xFF;
                }
                break;
        }
        case 3:
                sz_dat = 2; talk_buf = 1;
                dat[0] = 0x60 | (not_so_rand & 0x0f); dat[1] = 0x01;
                not_so_rand++;
                break;
        }
}

static void kbd_listen(void)
{
        if ((cur_cmd & 3) == 3 && dat[1] == 0xFE) kbd_addr = dat[0] & 0x0f;
}

static int any_event(void) { return !q_empty(); }

static void do_talk(void)
{
        uint8_t a = cur_cmd >> 4;
        if (a == mouse_addr) mouse_talk();
        else if (a == kbd_addr) kbd_talk();
}

static void end_listen(void)
{
        uint8_t a = cur_cmd >> 4;
        if (a == mouse_addr) mouse_listen();
        else if (a == kbd_addr) kbd_listen();
}

static void flush(void)
{
        uint8_t a = cur_cmd >> 4;
        if (a == kbd_addr || a == mouse_addr) { sz_dat = 2; talk_buf = 1; dat[0] = dat[1] = 0; }
}

/* ------------------------------------------------------------------ transceiver */

static void do_new_state(int state)
{
        ATRACE("[ADB state %d cmd %02x talk %d listen %d idx %d/%d]\n", state, cur_cmd, talk_buf, listen_buf, idx_dat, sz_dat);
        adb_int = 1;
        switch (state) {
        case 0:                                 /* start a new command */
                if (listen_buf) { listen_buf = 0; sz_dat = idx_dat; end_listen(); }
                talk_buf = 0; idx_dat = 0;
                cur_cmd = via_sr_shift_out();   /* also tells the Mac the command was taken */
                ATRACE("[ADB cmd %02x]\n", cur_cmd);
                switch ((cur_cmd >> 2) & 3) {
                case 0:
                        if ((cur_cmd & 3) == 0) { mouse_addr = 3; kbd_addr = 2; }   /* SendReset */
                        else if ((cur_cmd & 3) == 1) flush();
                        break;
                case 2: listen_buf = 1; break;
                case 3: do_talk(); break;
                }
                break;
        case 1:                                 /* transfer a data byte (even / odd) */
        case 2:
                if (!listen_buf) {
                        if (!talk_buf || idx_dat >= sz_dat) {
                                via_sr_rx(0xFF);
                                adb_int = 0;    /* no (more) data: "timeout" */
                        } else {
                                via_sr_rx(dat[idx_dat++]);
                        }
                } else {
                        uint8_t v = via_sr_shift_out();
                        if (idx_dat < sizeof dat) dat[idx_dat++] = v;
                }
                break;
        case 3:                                 /* idle */
                if (talk_buf) {
                        via_sr_rx(0xFF);
                } else if (any_event()) {
                        if (((cur_cmd >> 2) & 3) == 3) do_talk();
                        via_sr_rx(0xFF);
                }
                break;
        }
}

void    adb_set_state(int st, uint64_t now_us)
{
        state_now = st & 3;
        pending_state = state_now;
        pending_due = now_us + ADB_RESPONSE_US;
}

void    adb_tick(uint64_t now_us)
{
        if (pending_state >= 0 && now_us >= pending_due) {
                int s = pending_state;
                pending_state = -1;
                do_new_state(s);
        }
}

uint64_t adb_next_due(void) { return pending_state >= 0 ? pending_due : 0; }

/* 60 times a second (before the VBL): nudge the Mac when something is waiting */
void    adb_sixtieth(void)
{
        if (state_now == 3 && pending_state < 0 && !talk_buf && any_event()) {
                if (((cur_cmd >> 2) & 3) == 3) do_talk();
                via_sr_rx(0xFF);
        }
}
