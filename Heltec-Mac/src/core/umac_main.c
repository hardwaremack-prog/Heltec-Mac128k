/* umac
 *
 * Micro Mac 128K emulator
 *
 * Main file with:
 * - umac_ entry points,
 * - main loop,
 * - address decoding/memory map, dispatch to VIA/SCC/disc
 * - Keyboard/mouse event dispatch
 *
 * Copyright 2024 Matt Evans
 *
 * Small portions of m68k interrupt code, error handling taken from
 * Musashi, which is Copyright 1998-2002 Karl Stenerud.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#ifndef __wasm__
#include <setjmp.h>
#else
/* Heltec-Mac browser build: no setjmp in plain WebAssembly.  exit_error() just
 * flags the stop; the current slice finishes and umac_loop() returns 1. */
typedef int jmp_buf[1];
#define setjmp(b) 0
#define longjmp(b, v) return
#endif

#include "machw.h"
#include "m68k.h"
#include "via.h"
#include "scc.h"
#include "rom.h"
#include "adb.h"
#include "disc.h"

#ifdef PICO
#include "pico.h"
#define FAST_FUNC(x)    __not_in_flash_func(x)
#else
#define FAST_FUNC(x)    x
#endif


#ifdef DEBUG
#define MDBG(...)       printf(__VA_ARGS__)
#else
#define MDBG(...)       do {} while(0)
#endif

#define MERR(...)       fprintf(stderr, __VA_ARGS__)

/* Data */
static unsigned int g_int_controller_pending = 0;      /* list of pending interrupts */
static unsigned int g_int_controller_highest_int = 0;  /* Highest pending interrupt */

uint8_t *_ram_base;
unsigned int umac_ram_size = 128*1024;   /* Heltec-Mac: set before umac_init() */
uint8_t *_rom_base;

int overlay = 1;
int umac_is_se = 0;
static int in_exec = 0;
/* VIA E clock (CPU clock / 10; this emulator runs 8 CPU cycles per microsecond) */
static uint64_t umac_eclock(void);    /* Heltec-Mac: running a Mac SE ROM (ADB, SE overlay) */
static uint64_t global_time_us = 0;
static int sim_done = 0;
static jmp_buf main_loop_jb;

static int disassemble = 0;

#define UMAC_EXECLOOP_QUANTUM   5000

static void    update_overlay_layout(void);

////////////////////////////////////////////////////////////////////////////////

static int      m68k_dump_regs(char *buf, int len)
{
        int r;
        int orig_len = len;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "D%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_D0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "A%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_A0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        r = snprintf(buf, len, "SR: %08x  SP: %08x USP: %08x ISP: %08x MSP: %08x\n",
                     m68k_get_reg(NULL, M68K_REG_SR),
                     m68k_get_reg(NULL, M68K_REG_SP),
                     m68k_get_reg(NULL, M68K_REG_USP),
                     m68k_get_reg(NULL, M68K_REG_ISP),
                     m68k_get_reg(NULL, M68K_REG_MSP));
        return orig_len - len;
}

/* Exit with an error message.  Use printf syntax. */
static int guard_val = 0;
void exit_error(char* fmt, ...)
{
	char buff[500];
	unsigned int pc;
	va_list args;

	if(guard_val)
		return;
	else
		guard_val = 1;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	pc = m68k_get_reg(NULL, M68K_REG_PPC);
	m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	fprintf(stderr, "At %04x: %s\n", pc, buff);

        m68k_dump_regs(buff, 500);
        fprintf(stderr, "%s", buff);
        sim_done = 1;
        longjmp(main_loop_jb, 1);
}


////////////////////////////////////////////////////////////////////////////////
// VIA-related controls
static uint8_t via_ra_val = 0, via_rb_val = 0x80;   /* Heltec-Mac: for sound */

static void     via_ra_changed(uint8_t val)
{
        via_ra_val = val;
        static uint8_t oldval = 0x10;
        // 7 = scc w/req,a,b (in, indicates RX pending, w/o IRQ)
        // 6 = vid.pg2 (screen buffer select)
        // 5 = hd.sel (SEL line, select head)
        // 4 = overlay
        // 3 = snd.pg2 (sound buffer select)
        // [2:0] = sound volume
        if (umac_is_se) {       /* SE: PA4 selects the drive; overlay goes on a ROM access */
                oldval = val;
                return;
        }
        overlay = !!(val & 0x10);
        if ((oldval ^ val) & 0x10) {
                MDBG("OVERLAY CHANGING\n");
                update_overlay_layout();
        }

        oldval = val;
}

////////////////////////////////////////////////////////////////////////////////
// Heltec-Mac: real-time clock chip (seconds counter + parameter RAM), on VIA port B:
// bit 0 = rtcData, bit 1 = rtcClk, bit 2 = rtcEnb (active low).  The Mac clocks a
// command byte in (MSB first, sampled on the rising clock edge); for reads the chip
// then presents each bit after a falling edge.  Commands (z = 1 for read):
//   z00x aa01  seconds byte aa (0 = lowest)     0011 0001  test register (write)
//   0011 0101  write-protect (bit 7)            z010 aa01  PRAM $10-$13
//   z1aa aa01  PRAM $00-$0F                     z011 1aaa + 0aaa aa00  extended PRAM
static uint32_t rtc_secs = 0;          /* Mac time: seconds since 1904, local */
static uint8_t  rtc_pram[20];
static uint8_t  rtc_xpram[256];
static int      rtc_wp = 0;
static int      rtc_pram_dirty = 0;
static uint8_t  rtc_last = 0x07;
static int      rtc_mode = 0;          /* 0 command, 1 xpram address, 2 write data, 3 sending */
static int      rtc_bits = 0, rtc_outbit = -1;
static uint8_t  rtc_shift = 0, rtc_cmd = 0, rtc_out = 0;
static int      rtc_xaddr = 0;

static uint8_t *rtc_reg(uint8_t c, int *is_secs)
{
        int reg = (c >> 2) & 0x1f;
        *is_secs = -1;
        if (reg < 8) { *is_secs = reg & 3; return NULL; }
        if (reg >= 8 && reg < 12) return &rtc_pram[16 + (reg & 3)];
        if (reg >= 16) return &rtc_pram[reg & 15];
        return NULL;
}

static void     rtc_command(uint8_t c)
{
        int is_secs;
        rtc_cmd = c;
        if ((c & 0x78) == 0x38) {                       /* extended PRAM: one more byte */
                rtc_xaddr = (c & 7) << 5;
                rtc_mode = 1;
                return;
        }
        if (c & 0x80) {                                  /* read */
                uint8_t *p = rtc_reg(c, &is_secs);
                rtc_out = is_secs >= 0 ? (rtc_secs >> (8 * is_secs)) & 0xff : (p ? *p : 0);
                rtc_mode = 3; rtc_outbit = -1;
        } else {
                rtc_mode = 2;                            /* write: data byte follows */
        }
}

static void     rtc_byte(uint8_t b)
{
        int is_secs;
        if (rtc_mode == 0) { rtc_command(b); return; }
        if (rtc_mode == 1) {                             /* extended PRAM address */
                rtc_xaddr |= (b >> 2) & 0x1f;
                if (rtc_cmd & 0x80) { rtc_out = rtc_xpram[rtc_xaddr]; rtc_mode = 3; rtc_outbit = -1; }
                else rtc_mode = 2;
                return;
        }
        /* rtc_mode == 2: the data byte of a write */
        rtc_mode = 0;
        if ((rtc_cmd & 0x78) == 0x38) {
                if (!rtc_wp && rtc_xpram[rtc_xaddr] != b) { rtc_xpram[rtc_xaddr] = b; rtc_pram_dirty = 1; }
                return;
        }
        if (rtc_cmd == 0x35) { rtc_wp = !!(b & 0x80); return; }
        if (rtc_cmd == 0x31) return;
        if (rtc_wp) return;
        uint8_t *p = rtc_reg(rtc_cmd, &is_secs);
        if (is_secs >= 0) {
                rtc_secs = (rtc_secs & ~(0xffu << (8 * is_secs))) | ((uint32_t)b << (8 * is_secs));
        } else if (p && *p != b) {
                *p = b; rtc_pram_dirty = 1;
        }
}

static void     rtc_port(uint8_t val)
{
        int enb = val & 4, clk = val & 2, last_clk = rtc_last & 2;
        if (enb) {                                       /* deselected: start over */
                rtc_mode = 0; rtc_bits = 0; rtc_outbit = -1;
        } else if (!last_clk && clk) {                   /* rising edge */
                if (rtc_mode != 3) {
                        rtc_shift = (rtc_shift << 1) | (val & 1);
                        if (++rtc_bits == 8) { rtc_bits = 0; rtc_byte(rtc_shift); }
                }
        } else if (last_clk && !clk) {                   /* falling edge */
                if (rtc_mode == 3) {
                        if (++rtc_outbit >= 8) { rtc_mode = 0; rtc_outbit = -1; rtc_bits = 0; }
                }
        }
        rtc_last = val;
}

static int      rtc_data_out(void)
{
        if (rtc_mode != 3 || rtc_outbit < 0 || rtc_outbit > 7) return 1;
        return (rtc_out >> (7 - rtc_outbit)) & 1;
}

/* API */
void     umac_rtc_set(uint32_t mac_seconds) { rtc_secs = mac_seconds; }
uint32_t umac_rtc_get(void) { return rtc_secs; }
void     umac_rtc_tick(void) { rtc_secs++; }
uint8_t *umac_rtc_pram(void) { return rtc_pram; }
int      umac_rtc_pram_dirty(void) { int d = rtc_pram_dirty; rtc_pram_dirty = 0; return d; }
/* Also set the running system's clock (the low-memory global Time), once the OS is up */
void     umac_set_time_now(uint32_t mac_seconds)
{
        rtc_secs = mac_seconds;
        if (!overlay)
                RAM_WR32(0x20c, mac_seconds);
}

static void     via_rb_changed(uint8_t val)
{
        // 7 = sndres (sound enable/disable)
        // 6 = hblank
        // 5 = mouse8 (in, mouse Y2)
        // 4 = mouse4 (in, mouse X2)
        // 3 = mouse7 (in, 0 = button pressed)
        // [2:0] = RTC controls
        if (umac_is_se) {
                /* ADB state lines ST0 = PB4, ST1 = PB5; an input pin floats high */
                static int last_st = 3;
                uint8_t ddr = via_ddrb();
                int st = (((val & ddr) | (~ddr & 0x30)) >> 4) & 3;
                if (st != last_st) {
                        last_st = st;
                        adb_set_state(st, global_time_us);
                }
        }
        via_rb_val = val;
        rtc_port(val);
}

static uint8_t  via_ra_in(void)
{
        return 0;
}

// Quadrature bits read from B[5:4] (Y=5, X=4)
static uint8_t via_quadbits = 0;
static uint8_t via_mouse_pressed = 0;

static uint8_t  via_rb_in(void)
{
        if (umac_is_se)         /* SE: PB3 = ADB interrupt; PB4/5 are outputs */
                return rtc_data_out() | (adb_int_line() ? 0x08 : 0);
        uint8_t v = via_quadbits | rtc_data_out();
        // Mouse not pressed!
        if (!via_mouse_pressed)
                v |= (1 << 3);
        return v;
}

/* Keyboard interface:
 *
 * Very roughly, it uses CB2 as bidirectional data and CB1 as clock
 * always from keyboard.  There's a handshake with the mac driving
 * data low as a "request to start clocking", with the kbd receiving a
 * byte (clocking out from SR) after that.  The mac does this by
 * "transmitting a byte" of all zeroes, which looks like pulling data
 * low.
 *
 * The VIA SR has a sequence of interrupts as follows:
 * - Mac pulls data low (transmits zero) then immediately loads SR
 *   with the data to TX (a command such as Inquiry)
 * - The VIA asserts SR IRQ when the command's transmitted (the kbd
 *   has woken and clocked it out).
 * - The keyboard -- some time later, importantly -- responds with
 *   a byte in SR, and VIA asserts SR IRQ again.
 *
 * The keyboard does nothing except for respond to commands from the
 * host (i.e. there's nothing proactively transmitted).
 */
#define KBD_CMD_GET_MODEL       0x16
#define KBD_CMD_INQUIRY         0x10
#define KBD_CMD_INSTANT         0x14
#define KBD_MODEL               5
#define KBD_RSP_NULL            0x7b

static int kbd_last_cmd = 0;
static uint64_t kbd_last_cmd_time = 0;

static void     via_sr_tx(uint8_t data)
{
        if (kbd_last_cmd) {
                MDBG("KBD: Oops, transmitting %02x whilst cmd %02x pending!\n",
                     data, kbd_last_cmd);
        }
        kbd_last_cmd = data;
        kbd_last_cmd_time = global_time_us;
}

static int kbd_pending_evt = -1;
/* Emulate the keyboard: receive commands (such as an inquiry, polling
 * for keypresses) and respond using via_sr_rx().
 */
static void     kbd_rx(uint8_t data)
{
        /* Respond to requests with potted keyboard banter */
        switch (data) {
        case KBD_CMD_GET_MODEL:
                via_sr_rx(0x01 | (KBD_MODEL << 1));
                break;
        case KBD_CMD_INSTANT:   /* Heltec-Mac: answered like Inquiry (used after a 0x79 keypad prefix) */
        case KBD_CMD_INQUIRY:
                if (kbd_pending_evt == -1) {
                        via_sr_rx(KBD_RSP_NULL);
                } else {
                        via_sr_rx(kbd_pending_evt);
                        kbd_pending_evt = -1;
                }
        break;

        default:
                MERR("KBD: Unhandled TX %02x\n", data);
        }
}

static void     kbd_check_work(void)
{
        /* Process a keyboard command a little later than the transmit
         * time (i.e. not immediately, which makes the mac feel rushed
         * and causes it to ignore the response to punish our
         * hastiness).
         */
        if (kbd_last_cmd &&
            ((global_time_us - kbd_last_cmd_time) > UMAC_EXECLOOP_QUANTUM)) {
                MDBG("KBD: got cmd 0x%x\n", kbd_last_cmd);
                kbd_rx(kbd_last_cmd);
                kbd_last_cmd = 0;
        }
}

/* Heltec-Mac: true when the keyboard can take another event (no queue in here) */
int     umac_kbd_ready(void)
{
        return kbd_pending_evt == -1;
}

void    umac_kbd_event(uint8_t scancode, int down)
{
        if (kbd_pending_evt >= 0) {
                MDBG("KBD: Received event %02x with event %02x pending!\n",
                     scancode, kbd_pending_evt);
                /* FIXME: Add a queue */
        }
        kbd_pending_evt = scancode | (down ? 0 : 0x80);
}

// VIA IRQ output hook:
static void     via_irq_set(int status)
{
        MDBG("[IRQ: VIA IRQ %d]\n", status);
        if (status) {
                // IRQ is asserted
                m68k_set_virq(1, 1);
        } else {
                // IRQ de-asserted
                m68k_set_virq(1, 0);
        }
}

// Ditto, for SCC
static int scc_irq_state = 0;
static void     scc_irq_set(int status)
{
        MDBG("[IRQ: SCC IRQ %d]\n", status);
        if (status) {
                m68k_set_virq(2, 1);
        } else {
                m68k_set_virq(2, 0);
        }
        scc_irq_state = status;
}

// Heltec-Mac: the printer port (SCC channel B) goes to whoever registered for it
static int  (*printer_ready)(void);
static void (*printer_out)(uint8_t b);
void    umac_set_printer(int (*ready)(void), void (*out)(uint8_t b))
{
        printer_ready = ready;
        printer_out = out;
}
static int      scc_tx_ready(int ch)
{
        if (ch != 0 || !printer_ready)
                return 1;
        return printer_ready();
}
static void     scc_tx(int ch, uint8_t data)
{
        if (ch == 0 && printer_out)
                printer_out(data);
}

////////////////////////////////////////////////////////////////////////////////
// IWM

static uint8_t iwm_regs[16];

void    iwm_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 9) & 0xf;
        MDBG("[IWM: WR %02x -> %d]\n", data, r);
        switch (r) {
        default:
                MDBG("[IWM: unhandled WR %02x to reg %d]\n", data, r);
        }
        iwm_regs[r] = data;
}

uint8_t iwm_read(unsigned int address)
{
        unsigned int r = (address >> 9) & 0xf;
        uint8_t data = iwm_regs[r];
        switch (r) {
        case 8:
                data = 0xff;
                break;
        case 14:
                data = 0x1f;
                break;
        default:
                MDBG("[IWM: unhandled RD of reg %d]\n", r);
        }
        MDBG("[IWM: RD %d <- %02x]\n", r, data);
        return data;
}

////////////////////////////////////////////////////////////////////////////////

static unsigned int  FAST_FUNC(cpu_read_instr_normal)(unsigned int address)
{
        /* Can check for 0x400000 (ROM) and otherwise RAM */
        if ((address & 0xf00000) != ROM_ADDR)
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
        else
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
}

/* Mac SE: the boot overlay switches off on the first access to the ROM's real address */
static void     se_overlay_off(void)
{
        overlay = 0;
        update_overlay_layout();
}
#define SE_OVERLAY_CHECK(a) do { if (overlay && umac_is_se && (ADR24(a) & 0xf00000) == ROM_ADDR) se_overlay_off(); } while (0)

static unsigned int  FAST_FUNC(cpu_read_instr_overlay)(unsigned int address)
{
        SE_OVERLAY_CHECK(address);
        /* Need to check for both 0=ROM, 0x400000=ROM, and RAM at 0x600000...
         */
        if (IS_ROM(address))
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
        else /* RAM */
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
}

unsigned int (*cpu_read_instr)(unsigned int address) = cpu_read_instr_overlay;

/* Read data from RAM, ROM, or a device */
unsigned int    FAST_FUNC(cpu_read_byte)(unsigned int address)
{
        SE_OVERLAY_CHECK(address);
        /* Most likely a RAM access, followed by a ROM access, then I/O */
        if (IS_RAM(address))
                return RAM_RD8(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD8(address & (ROM_SIZE - 1));

        // decode IO etc
        if (IS_VIA(address))
                return via_read(address);
        if (IS_IWM(address))
                return iwm_read(address);
        if (IS_SCC_RD(address))
                return scc_read(address);
        if (IS_DUMMY(address))
                return 0;

        printf("Attempted to read byte from address %08x\n", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_word)(unsigned int address)
{
        SE_OVERLAY_CHECK(address);
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));

        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read word from address %08x", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_long)(unsigned int address)
{
        SE_OVERLAY_CHECK(address);
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));

        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read long from address %08x", address);
        return 0;
}


unsigned int    cpu_read_word_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));

        exit_error("Disassembler attempted to read word from address %08x", address);
        return 0;
}

unsigned int    cpu_read_long_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));

        exit_error("Dasm attempted to read long from address %08x", address);
        return 0;
}


/* Write data to RAM or a device */
#ifdef MACMESH_HOST
static void mem_watch(unsigned int address, unsigned int value, int size)
{
        static int mw = -1;
        if (mw < 0) { const char *e = getenv("MEMWATCH"); mw = e ? (int)strtoul(e, 0, 16) : 0; }
        if (mw && ADR24(address) <= (unsigned)mw && ADR24(address) + size > (unsigned)mw)
                fprintf(stderr, "[MEM %06x <- %x (size %d) at PC %06x]\n", ADR24(address), value, size, m68k_get_reg(NULL, M68K_REG_PPC));
}
#define MEMWATCH(a, v, s) mem_watch(a, v, s)
#else
#define MEMWATCH(a, v, s) do {} while (0)
#endif

void    FAST_FUNC(cpu_write_byte)(unsigned int address, unsigned int value)
{
        MEMWATCH(address, value, 1);
        if (IS_RAM(address)) {
                RAM_WR8(CLAMP_RAM_ADDR(address), value);
                return;
        }

        // decode IO
        if (IS_VIA(address)) {
                via_write(address, value);
                return;
        }
        if (IS_IWM(address)) {
                iwm_write(address, value);
                return;
        }
        if (IS_SCC_WR(address)) {
                scc_write(address, value);
                return;
        }
        if (IS_DUMMY(address))
                return;
        if (address == PV_SONY_ADDR) {
                int r = disc_pv_hook(value);
                if (r)
                        exit_error("Disc PV hook failed (%02x)", value);
                return;
        }
        printf("Ignoring write %02x to address %08x\n", value&0xff, address);
}

void    FAST_FUNC(cpu_write_word)(unsigned int address, unsigned int value)
{
        MEMWATCH(address, value, 2);
        if (IS_RAM(address)) {
                RAM_WR16(CLAMP_RAM_ADDR(address), value);
                return;
        }
        printf("Ignoring write %04x to address %08x\n", value&0xffff, address);
}

void    FAST_FUNC(cpu_write_long)(unsigned int address, unsigned int value)
{
        MEMWATCH(address, value, 4);
        if (IS_RAM(address)) {
                RAM_WR32(CLAMP_RAM_ADDR(address), value);
                return;
        }
        printf("Ignoring write %08x to address %08x\n", value, address);
}

/* Update function pointers for memory accessors based on overlay state/memory map layout */
static void     update_overlay_layout(void)
{
        if (overlay) {
                cpu_read_instr = cpu_read_instr_overlay;
        } else {
                cpu_read_instr = cpu_read_instr_normal;
        }
}

/* Called when the CPU pulses the RESET line */
void    cpu_pulse_reset(void)
{
        /* Reset IRQs etc. */
}

/* Called when the CPU acknowledges an interrupt */
int     cpu_irq_ack(int level)
{
        (void)level;
        /* Level really means line, so do an ack per device */
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Implementation for the interrupt controller */
void    int_controller_set(unsigned int value)
{
	unsigned int old_pending = g_int_controller_pending;

	g_int_controller_pending |= (1<<value);

	if(old_pending != g_int_controller_pending && value > g_int_controller_highest_int)
	{
		g_int_controller_highest_int = value;
		m68k_set_irq(g_int_controller_highest_int);
	}
}

void    int_controller_clear(unsigned int value)
{
	g_int_controller_pending &= ~(1<<value);

	for(g_int_controller_highest_int = 7;g_int_controller_highest_int > 0;g_int_controller_highest_int--)
		if(g_int_controller_pending & (1<<g_int_controller_highest_int))
			break;

	m68k_set_irq(g_int_controller_highest_int);
}

/* Disassembler */
static void     make_hex(char* buff, unsigned int pc, unsigned int length)
{
	char* ptr = buff;

	for(;length>0;length -= 2)
	{
		sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
		pc += 2;
		ptr += 4;
		if(length > 2)
			*ptr++ = ' ';
	}
}

void    cpu_instr_callback(int pc)
{
	static char buff[100];
	static char buff2[100];
	static unsigned int instr_size;
#ifdef MACMESH_HOST
        /* test hook: PCWATCH=403a54,403a8e prints when those addresses run */
        static int nwatch = -1; static unsigned int watch[16];
        if (nwatch < 0) {
                nwatch = 0;
                const char *w = getenv("PCWATCH");
                while (w && *w && nwatch < 16) { watch[nwatch++] = strtoul(w, (char **)&w, 16); if (*w == ',') w++; }
        }
        for (int i = 0; i < nwatch; i++)
                if ((unsigned int)pc == watch[i])
                        fprintf(stderr, "[PC %06x D0=%08x D1=%08x D2=%08x]\n", pc, m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2));
#endif

        if (!disassemble)
                return;

	instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	make_hex(buff2, pc, instr_size);
	MDBG("E %03x: %-20s: %s\n", pc, buff2, buff);
	fflush(stdout);
}

int     umac_init(void *ram_base, void *rom_base, disc_descr_t discs[DISC_NUM_DRIVES])
{
        _ram_base = ram_base;
        _rom_base = rom_base;
        umac_is_se = rom_is_se(rom_base);
        adb_reset();

	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();

        struct via_cb vcb = { .ra_change = via_ra_changed,
                              .rb_change = via_rb_changed,
                              .ra_in = via_ra_in,
                              .rb_in = via_rb_in,
                              .sr_tx = via_sr_tx,
                              .irq_set = via_irq_set,
        };
        via_init(&vcb);
        via_timers_enable(umac_is_se, umac_eclock);   /* SE: real VIA timers (Time Manager) */
        struct scc_cb scb = { .irq_set = scc_irq_set,
                              .tx_ready = scc_tx_ready,
                              .tx = scc_tx,
        };
        scc_init(&scb);
        disc_init(discs);

        return 0;
}

void    umac_opt_disassemble(int enable)
{
        disassemble = enable;
}

#define MOUSE_MAX_PENDING_PIX   30

static int pending_mouse_deltax = 0;
static int pending_mouse_deltay = 0;

/* Provide mouse input (movement, button) data.
 *
 * X is positive going right; Y is positive going upwards.
 */
void    umac_mouse(int deltax, int deltay, int button)
{
        pending_mouse_deltax += deltax;
        pending_mouse_deltay += deltay;

        /* Clamp if the UI has flooded with lots and lots of steps!
         */
        if (pending_mouse_deltax > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltax < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = -MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = -MOUSE_MAX_PENDING_PIX;

        /* FIXME: The movement might take a little time, but this
         * posts the button status immediately.  Probably OK, but the
         * mismatch might be perceptible.
         */
        via_mouse_pressed = button;
}

static void     mouse_tick(void)
{
        /* Periodically, check if the mouse X/Y deltas are non-zero.
         * If a movement is required, encode one step in X and/or Y
         * and deduct from the pending delta.
         *
         * The step ultimately posts an SCC IRQ, so we _don't_ try to
         * make any more steps while an IRQ is currently pending.
         * (Currently, that means a previous step's DCD IRQ event
         * hasn't yet been consumed by the OS handler.  In future, if
         * SCC is extended with other IRQ types, then just checking
         * the IRQ status is technically too crude, but should still
         * be fine given the timeframes.)
         */
        if (pending_mouse_deltax == 0 && pending_mouse_deltay == 0)
                return;

        if (scc_irq_state == 1)
                return;

        static int old_dcd_a = 0;
        static int old_dcd_b = 0;

        /* Mouse X/Y quadrature signals are wired to:
         *  VIA Port B[4] & SCC DCD_A for X
         *  VIA Port B[5] & SCC DCD_B for Y
         *
         * As VIA mouse signals aren't sampled until IRQ, can do this
         * in one step, toggling existing DCD states and setting VIA
         * either equal or opposite to DCD:
         */
        int dcd_a = old_dcd_a;
        int dcd_b = old_dcd_b;
        int deltax = pending_mouse_deltax;
        int deltay = pending_mouse_deltay;
        uint8_t qb = via_quadbits;

        if (deltax) {
                dcd_a = !dcd_a;
                qb = (qb & ~0x10) | ((deltax < 0) == dcd_a ? 0x10 : 0);
                pending_mouse_deltax += (deltax > 0) ? -1 : 1;
                MDBG("  px %d, oldpx %d", pending_mouse_deltax, deltax);
        }

        if (deltay) {
                dcd_b = !dcd_b;
                qb = (qb & ~0x20) | ((deltay < 0) == dcd_b ? 0x20 : 0);
                pending_mouse_deltay += (deltay > 0) ? -1 : 1;
                MDBG("  py %d, oldpy %d", pending_mouse_deltay, deltay);
        }
        MDBG("\n");

        via_quadbits = qb;
        old_dcd_a = dcd_a;
        old_dcd_b = dcd_b;
        scc_set_dcd(dcd_a, dcd_b);
}

/* Heltec-Mac: absolute pointer for VNC.  Writes the new position into the
 * Mac's low-memory cursor globals (MTemp, RawMouse) and asks the cursor
 * task to redraw (CrsrNew = CrsrCouple), the same trick Mini vMac uses.
 * Only done once the OS is running (RAM mapped at 0, globals sensible).
 */
void    umac_mouse_abs(int x, int y, int button)
{
        if (umac_is_se && (!!button) != via_mouse_pressed)
                adb_mouse_button(button);       /* SE: the button travels over ADB */
        via_mouse_pressed = !!button;
        if (overlay)
                return;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= DISP_WIDTH) x = DISP_WIDTH - 1;
        if (y >= DISP_HEIGHT) y = DISP_HEIGHT - 1;
        /* Point is stored v,h (y then x) */
        RAM_WR16(0x828, y);  RAM_WR16(0x82a, x);     /* MTemp */
        RAM_WR16(0x82c, y);  RAM_WR16(0x82e, x);     /* RawMouse */
        if (umac_is_se) {
                /* The SE ROM's cursor task moves the cursor by (MTemp - RawMouse), scaled for
                 * mouse acceleration, and leaves Mouse alone when that is zero.  With an
                 * absolute mouse there is no delta, so set Mouse too: the task then just
                 * redraws the cursor where we put it. */
                RAM_WR16(0x830, y);  RAM_WR16(0x832, x);     /* Mouse */
        }
        RAM_WR8(0x8ce, RAM_RD8(0x8cf));               /* CrsrNew = CrsrCouple */
}

/* Heltec-Mac: sound.  The Mac Plus plays 370 samples per video frame
 * (~22.25 kHz) from the main sound buffer at memtop-0x300: the high byte
 * of each word is an unsigned 8-bit sample.  Sound is on when VIA PB7
 * (sndres) is low; VIA PA[2:0] is the volume (0-7).  (The alternate sound
 * buffer, VIA PA3, is ignored.)  Returns 1 if sound is enabled.
 */
int     umac_sound_frame(uint8_t samples[370], int *volume)
{
#ifdef MACMESH_TRACE
        {
                static int n = 0; static uint8_t lra = 0xEE, lrb = 0xEE;
                int lo = 255, hi = 0;
                for (int i = 0; i < 370; i++) { int v = _ram_base[RAM_SIZE - 0x300 + i * 2]; if (v < lo) lo = v; if (v > hi) hi = v; }
                if (via_ra_val != lra || via_rb_val != lrb || (hi - lo) > 0 || (++n % 300) == 0)
                        fprintf(stderr, "snd f%d: RA=%02x (vol %d, buf%s) RB=%02x (sound %s) samples %d..%d\n", n,
                                via_ra_val, via_ra_val & 7, (via_ra_val & 8) ? "main" : "ALT", via_rb_val,
                                (via_rb_val & 0x80) ? "OFF" : "on", lo, hi);
                lra = via_ra_val; lrb = via_rb_val;
        }
#endif
        uint32_t base = RAM_SIZE - 0x300;
        for (int i = 0; i < 370; i++)
                samples[i] = _ram_base[base + i * 2];
        if (volume)
                *volume = via_ra_val & 7;
        return !(via_rb_val & 0x80);
}

int     umac_crashed(void)
{
        return sim_done;
}

void    umac_reset(void)
{
        overlay = 1;
        update_overlay_layout();
        sim_done = 0;
        guard_val = 0;
        kbd_pending_evt = -1;
        kbd_last_cmd = 0;
        /* Heltec-Mac: a restart in place also clears half-finished clock-chip and
         * mouse traffic, and the interrupt lines (umac_init has just set the chips up) */
        rtc_mode = 0; rtc_bits = 0; rtc_outbit = -1;
        pending_mouse_deltax = pending_mouse_deltay = 0;
        scc_irq_state = 0;
        adb_reset();
        m68k_set_virq(1, 0);
        m68k_set_virq(2, 0);
        m68k_pulse_reset();
}

/* Called by the disc code when an eject op happens. */
void    umac_disc_ejected(void)
{
        /* Heltec-Mac: the disk lives in flash and can't really leave; the Mac
         * just sees an empty drive until it's restarted (Restart Mac
         * re-inserts it).  Upstream umac rebooted here. */
}

/* Run the emulator for about a frame.
 * Returns 0 for not-done, 1 when an exit/done condition arises.
 */
static uint64_t umac_eclock(void)
{
        return (global_time_us * 8 + (in_exec ? (uint64_t)m68k_cycles_run() : 0)) / 10;
}

int     umac_loop(void)
{
        setjmp(main_loop_jb);
        in_exec = 0;

        const int us = UMAC_EXECLOOP_QUANTUM;
        if (umac_is_se) {
                /* ADB answers about 0.7 ms after each bus state change, so run the
                 * CPU in short slices and let ADB act in between */
                for (int t = 0; t < us; t += 500) {
                        in_exec = 1;
                        m68k_execute(500*8);
                        in_exec = 0;
                        global_time_us += 500;
                        adb_tick(global_time_us);
                        via_tick(global_time_us);
                }
        } else {
                m68k_execute(us*8);
                global_time_us += us;
        }

        // Device polling
        via_tick(global_time_us);
        mouse_tick();
        if (!umac_is_se)
                kbd_check_work();
        scc_poll();

	return sim_done;
}

