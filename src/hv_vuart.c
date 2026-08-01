/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "aic.h"
#include "iodev.h"
#include "uart.h"
#include "uart_regs.h"
#include "usb.h"

bool active = false;

u32 ucon = 0;
u32 utrstat = 0;
u32 ufstat = 0;

int vuart_irq = 0;

static void update_irq(void)
{
    // Pump USB events for …43 so the emulated PL011 (kd) still sees incoming host bytes.
    iodev_handle_events(IODEV_USB_VUART);

    // The S5L UART is console-out only now: its firmware DEBUG text is echoed to the m1n1
    // console (…41 / hv.log), while IODEV_USB_VUART (…43) belongs exclusively to the kd
    // PL011 port (hv_pl011.c). So it must NEVER report RX from …43 - doing so would steal
    // kd input bytes and raise phantom RX interrupts (a spurious-IRQ source).
    utrstat |= UTRSTAT_TXBE | UTRSTAT_TXE;
    utrstat &= ~UTRSTAT_RXD;
    ufstat = 0;

    if (FIELD_GET(UCON_TXMODE, ucon) == UCON_MODE_IRQ && ucon & UCON_TXTHRESH_ENA) {
        utrstat |= UTRSTAT_TXTHRESH;
    }

    if (vuart_irq) {
        uart_clear_irqs();
        if (utrstat & UTRSTAT_TXTHRESH) {
            aic_set_sw(vuart_irq, true);
        } else {
            aic_set_sw(vuart_irq, false);
        }
    }
}

static void handle_vuart_passthrough(uint8_t b)
{
    // Firmware S5L console -> m1n1 console (…41 / hv.log), kept off the kd channel. Each
    // line is prefixed so it is distinguishable from m1n1's own log output.
    static bool sol = true;

    if (b == '\r')
        return;
    if (b == '\n') {
        printf("\n");
        sol = true;
        return;
    }
    if (sol) {
        printf("FW> ");
        sol = false;
    }
    printf("%c", b);
}

static bool handle_vuart(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    UNUSED(ctx);
    UNUSED(width);

    // Second page of the UART block is the emulated PL011 for the Windows kernel
    // debugger (see hv_pl011.c). It shares this proven hook so it is actually mapped.
    if (addr & 0x1000)
        return pl011_reg(addr & 0xfff, val, write);

    addr &= 0xfff;

    update_irq();

    if (write) {
        //         printf("HV: vuart W 0x%lx <- 0x%lx (%d)\n", addr, *val, width);
        switch (addr) {
            case UCON:
                ucon = *val;
                break;
            case UTXH: {
                uint8_t b = *val;
                // Firmware console goes to the m1n1 log channel only; …43 is kd-only now.
                handle_vuart_passthrough(b);
                break;
            }
            case UTRSTAT:
                utrstat &= ~(*val & (UTRSTAT_TXTHRESH | UTRSTAT_RXTHRESH | UTRSTAT_RXTO));
                break;
        }
    } else {
        switch (addr) {
            case UCON:
                *val = ucon;
                break;
            case URXH:
                // No host input to the firmware UART; …43 input belongs to kd (PL011).
                *val = 0;
                break;
            case UTRSTAT:
                *val = utrstat;
                break;
            case UFSTAT:
                //
                // HACK HACK: the below code needs to account for whether we require SAM5250 semantics for the Windows
                // UART driver or if we can get away with using the 8900 UART raw (they're basically compatible but not fully.)
                //
                *val = utrstat & UTRSTAT_TXBE ? ufstat : ufstat | BIT(24);
                break;
            case UERSTAT:
                *val = 0;
                break;
            default:
                *val = 0;
                break;
        }
        //         printf("HV: vuart R 0x%lx -> 0x%lx (%d)\n", addr, *val, width);
    }

    return true;
}

void hv_vuart_poll(void)
{
    if (!active)
        return;

    update_irq();
}

void hv_map_vuart(u64 base, int irq, iodev_id_t iodev)
{
    hv_map_hook(base, handle_vuart, 0x2000); // page 1: S5L UART; page 2: emulated PL011 (kd)
    usb_iodev_vuart_setup(iodev);
    vuart_irq = irq;
    active = true;
}
