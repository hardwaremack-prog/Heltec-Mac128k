/* SCC 85C30 model: just enough to model DCD interrupts.
 * God, I hate this chip, it's fugly AF.
 *
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

#include <stdio.h>
#include <inttypes.h>

#include "scc.h"

#if defined(DEBUG) || defined(SCC_TRACE)
#define SDBG(...)       fprintf(stderr, __VA_ARGS__)
#else
#define SDBG(...)       do {} while(0)
#endif

#define SERR(...)       fprintf(stderr, __VA_ARGS__)


////////////////////////////////////////////////////////////////////////////////
// SCC
static uint8_t scc_reg_ptr = 0;
static uint8_t scc_mie = 0;
static uint8_t scc_read_acks = 0;
static uint8_t scc_status_hi = 0;
static uint8_t scc_ie[2] = {0};
#define SCC_IE_ZEROCOUNT        0x02
#define SCC_IE_DCD              0x08
#define SCC_IE_SYNCHUNT         0x10
#define SCC_IE_CTS              0x20
#define SCC_IE_TXUNDER          0x40
#define SCC_IE_ABORT            0x80
static uint8_t scc_irq_pending = 0;
#define SCC_IP_B_EXT            0x01
#define SCC_IP_B_TX             0x02
#define SCC_IP_A_EXT            0x08
#define SCC_IP_A_TX             0x10

/* Heltec-Mac: transmitter, per channel (index = AnB: 0 = B, 1 = A) */
static uint8_t scc_txie[2] = {0};       // WR1 bit 1
static uint8_t scc_txbe[2] = {1, 1};    // Tx buffer empty
static int     scc_txheld[2] = {-1, -1}; // byte waiting for the host
static const uint8_t scc_ip_tx[2] = {SCC_IP_B_TX, SCC_IP_A_TX};

static uint8_t scc_vec = 0;
static uint8_t scc_irq = 0;
static struct scc_cb scc_callbacks;

static uint8_t scc_dcd_pins = 0;        // 0 = A, 1 = B
static uint8_t scc_dcd_a_changed = 0;
static uint8_t scc_dcd_b_changed = 0;

static void     scc_assess_irq(void);

////////////////////////////////////////////////////////////////////////////////

void    scc_init(struct scc_cb *cb)
{
        if (cb)
                scc_callbacks = *cb;
        /* Heltec-Mac: start from a clean chip (scc_init also runs on a Mac restart) */
        scc_reg_ptr = scc_mie = scc_read_acks = scc_status_hi = 0;
        scc_ie[0] = scc_ie[1] = 0;
        scc_irq_pending = 0;
        scc_vec = 0;
        scc_txie[0] = scc_txie[1] = 0;
        scc_txbe[0] = scc_txbe[1] = 1;
        scc_txheld[0] = scc_txheld[1] = -1;
        scc_dcd_a_changed = scc_dcd_b_changed = 0;
        if (scc_irq && scc_callbacks.irq_set)
                scc_callbacks.irq_set(0);
        scc_irq = 0;
}

void    scc_set_dcd(int a, int b)
{
        uint8_t v = (a ? 1 : 0) | (b ? 2 : 0);
        if ((v ^ scc_dcd_pins) & 1)
                scc_dcd_a_changed = 1;
        if ((v ^ scc_dcd_pins) & 2)
                scc_dcd_b_changed = 1;
        scc_dcd_pins = v;

        scc_assess_irq();
}

////////////////////////////////////////////////////////////////////////////////

// WR0: Reg pointers, command
static void     scc_wr0(int AnB, uint8_t data)
{
        scc_reg_ptr = data & 7;

        if (data & 0xc0) {
                // Reset commands, e.g. CRC generators, EOM latch
        }
        int cmd = (data & 0x38) >> 3;
        switch (cmd) {
        case 0: // Null
                break;
        case 1: // Point high
                scc_reg_ptr |= 8;
                break;
        case 5: // Reset Tx interrupt pending
                scc_irq_pending &= ~scc_ip_tx[AnB];
                break;
        case 2: // Reset Ext/Status interrupts
        case 7: // Reset highest IUS
                break;
                // 2: reset Ext/Status IRQs
                // enables RR0 status to be re-latched (cause IRQ again if sometihng's pending?)
        default:
                SDBG("(SCC WR0: Command %d unhandled!)\n", cmd);
        }
}

// WR1: interrupt enables (bit 1 = Tx interrupt enable)
static void     scc_wr1(int AnB, uint8_t data)
{
        scc_txie[AnB] = !!(data & 0x02);
        if (!scc_txie[AnB])
                scc_irq_pending &= ~scc_ip_tx[AnB];
}

// Transmit: hand the byte to the host now, or hold it until the host has room
static void     scc_tx_done(int AnB)
{
        scc_txbe[AnB] = 1;
        if (scc_txie[AnB])
                scc_irq_pending |= scc_ip_tx[AnB];
}

static void     scc_tx_byte(int AnB, uint8_t data)
{
        if (scc_callbacks.tx_ready && !scc_callbacks.tx_ready(AnB)) {
                scc_txheld[AnB] = data;
                scc_txbe[AnB] = 0;
                scc_irq_pending &= ~scc_ip_tx[AnB];
                return;
        }
        if (scc_callbacks.tx)
                scc_callbacks.tx(AnB, data);
        scc_irq_pending &= ~scc_ip_tx[AnB];
        scc_tx_done(AnB);
}

void    scc_poll(void)
{
        for (int c = 0; c < 2; c++) {
                if (scc_txheld[c] < 0)
                        continue;
                if (scc_callbacks.tx_ready && !scc_callbacks.tx_ready(c))
                        continue;
                if (scc_callbacks.tx)
                        scc_callbacks.tx(c, (uint8_t)scc_txheld[c]);
                scc_txheld[c] = -1;
                scc_tx_done(c);
                scc_assess_irq();
        }
}

// WR2: Interrupt vector
static void     scc_wr2(uint8_t data)
{
        scc_vec = data;
}

// WR3: Receive Parameters & Control
static void     scc_wr3(int AnB, uint8_t data)
{
        (void)AnB;
        // Keep an eye out for bit 0x10 (enter hunt mode), and external/status is asserted

        if (data & 0x10) {
                // Enter hunt mode (Don't really need to do anything, 7.5.5 doesn't care)
        }
}

// WR9: Master Interrupt control and reset commands
static void     scc_wr9(uint8_t data)
{
        // 7:8 = Various reset commands, channel A/B/HW reset
        if (data & 0xc0) {
        }
        scc_mie = !!(data & 0x08);
        scc_read_acks = !!(data & 0x20);
        scc_status_hi = !!(data & 0x10);
}

// WR15: External status interrupt enable control
static void     scc_wr15(int AnB, uint8_t data)
{
        scc_ie[AnB] = data;
}

// RR0: Transmit and Receive buffer status and external status
static uint8_t  scc_rr0(int AnB)
{
        uint8_t v = 0;
        // [3]: If IE[channel].DCD = 0, reports /DCD pin state.  Else, reports
        //      state as of last pin change (?).. samples as of IRQ?
        //
        // FIXME: sample it?
        if (AnB) {
                v = (scc_dcd_pins & 1) ? 0x08 : 0;
        } else {
                v = (scc_dcd_pins & 2) ? 0x08 : 0;
        }
        // (Other bits: [2] = TX empty, [5] = /CTS)
        v |= 0x10; // Sync/Hunt status (set on reset/by hunt?)
        v |= 0x40; // TxUnderrun/EOM
        if (scc_txbe[AnB])
                v |= 0x04; // Tx buffer empty
        /* CTS (bit 5) stays clear: the Mac's handshake input is inverted, and
         * the ImageWriter driver reads 0 here as "printer ready" */

        return v;
}

// RR1: Special Receive condition
static uint8_t  scc_rr1(int AnB)
{
        uint8_t v = 0x01 /* All sent */ | 0x06 /* SDLC, set to 011 on channel reset */;
        (void)AnB;
        // Note, not really necessary (7.5.5 is OK to return 0) but A Bit Better
        return v;
}

// RR2:
// Some special behaviour; if scc_read_acks, then a read will do an ack, de-assert IRQ
// If read from A, raw vector.  If read from B, "modified vector" == ?
static uint8_t  scc_rr2(int AnB)
{
        if (AnB)
                return scc_vec;

        //status high shift << 3
        // B external = 001
        // A external = 101
        // Priority: A Tx > A Ext > B Tx > B Ext.  Tx pending is cleared by
        // writing data or WR0 'reset Tx IP', not by reading the vector.
        uint8_t v;
        if (scc_irq_pending & SCC_IP_A_TX) {
                v = 4;
        } else if (scc_irq_pending & SCC_IP_A_EXT) {
                v = 5;
                scc_irq_pending &= ~SCC_IP_A_EXT;
        } else if (scc_irq_pending & SCC_IP_B_TX) {
                v = 0;
        } else if (scc_irq_pending & SCC_IP_B_EXT) {
                v = 1;
                scc_irq_pending &= ~SCC_IP_B_EXT;
        } else {
                v = 3; // nothing pending
        }
        // VIS

        // FIXME: consume/clear in pending..?
        //
        if (scc_status_hi)
                v = (scc_vec & 0x8f) | (v << 4);
        else
                v = (scc_vec & 0xf1) | (v << 1);
        return v;
}

// RR3: Interrupt Pending Register (A only)
static uint8_t  scc_rr3(int AnB)
{
        if (!AnB)
                return 0;
        return scc_irq_pending;
}

// RR15: Reflects WR15 (interrupt enables)
static uint8_t  scc_rr15(int AnB)
{
        return scc_ie[AnB] & 0xfa;
}

////////////////////////////////////////////////////////////////////////////////

// Call after a state change: checks MIE, interrupt enables,
// and (de)asserts IRQ output if necessary
static void     scc_assess_irq(void)
{
        if (scc_dcd_a_changed && (scc_ie[1] & SCC_IE_DCD)) {
                scc_irq_pending |= SCC_IP_A_EXT;
                scc_dcd_a_changed = 0;
        }
        if (scc_dcd_b_changed && (scc_ie[0] & SCC_IE_DCD)) {
                scc_irq_pending |= SCC_IP_B_EXT;
                scc_dcd_b_changed = 0;
        }

        if (scc_irq_pending && scc_mie) {
                if (!scc_irq) {
                        if (scc_callbacks.irq_set)
                                scc_callbacks.irq_set(1);
                        scc_irq = 1;
                }
        } else {
                // No IRQ.  Drop line if it was asserted:
                if (scc_irq) {
                        if (scc_callbacks.irq_set)
                                scc_callbacks.irq_set(0);
                        scc_irq = 0;
                }
        }
}

void    scc_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 1) & 0x3;
        unsigned int AnB = !!(r & 1);
        unsigned int DnC = !!(r & 2);

        SDBG("[SCC: Write %x %02x]: ", address, data);

        if (DnC) {
                scc_tx_byte(AnB, data);
        } else {
                SDBG("[SCC: WR %02x -> WR%d%c]\n",
                     data, scc_reg_ptr, 'B' - AnB);

                switch (scc_reg_ptr) {
                case 0:
                        scc_wr0(AnB, data);
                        break;
                case 1:
                        scc_wr1(AnB, data);
                        scc_reg_ptr = 0;
                        break;
                case 2:
                        scc_wr2(data);
                        scc_reg_ptr = 0;
                        break;
                case 3:
                        scc_wr3(AnB, data);
                        scc_reg_ptr = 0;
                        break;
                case 9:
                        scc_wr9(data);
                        scc_reg_ptr = 0;
                        break;
                case 15:
                        scc_wr15(AnB, data);
                        scc_reg_ptr = 0;
                        break;
                default:
                        SDBG("[SCC: unhandled WR %02x to reg %d]\n",
                             data, scc_reg_ptr);
                        scc_reg_ptr = 0;
                }
        }
        scc_assess_irq();
}

uint8_t scc_read(unsigned int address)
{
        uint8_t data = 0;
        unsigned int r = (address >> 1) & 0x3;
        unsigned int AnB = !!(r & 1);
        unsigned int DnC = !!(r & 2);

        SDBG("[SCC: Read %x]: ", address);
        if (DnC) {
                SDBG("[SCC: Data read (%c) ignored]\n", 'B' - AnB);
        } else {
                SDBG("[SCC: RD <- RR%d%c = ",
                     scc_reg_ptr, 'B' - AnB);

                switch (scc_reg_ptr) {
                case 0:
                        data = scc_rr0(AnB);
                        break;
                case 1:
                        data = scc_rr1(AnB);
                        break;
                case 2:
                        data = scc_rr2(AnB);
                        break;
                case 3:
                        data = scc_rr3(AnB);
                        break;
                case 15:
                        data = scc_rr15(AnB);
                        break;
                default:
                        SDBG("(unhandled!) ");
                        data = 0;
                }
                SDBG("%02x]\n", data);
        }
        // Reads always reset the pointer
        scc_reg_ptr = 0;
        return data;
}

