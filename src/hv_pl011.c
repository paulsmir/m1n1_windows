/* SPDX-License-Identifier: MIT */

//
// Emulated ARM PL011 UART for the Windows kernel debugger (kdcom speaks 16550/PL011, not
// the Apple/Samsung S5L UART that hv_vuart emulates). It lives in the SECOND page of the
// Apple UART's MMIO block (base + 0x1000): that page is already backed by a working
// stage-2 hook (hv_vuart's), whereas an arbitrary standalone address is not mapped at all
// and faults. hv_vuart.c dispatches page-2 accesses here; the firmware's DBG2 points
// kdcom at base + 0x1000. Bytes are forwarded raw (no console echo - kd is binary)
// to/from the host over IODEV_USB_VUART, the same channel the S5L UART uses.
//
// Only what kdcom touches is modelled: DR (data), FR (TX-ready / RX-empty), and the
// PrimeCell identification registers so the driver recognises the part.
//

#include "hv.h"
#include "iodev.h"
#include "types.h"
#include "utils.h"

#define PL011_DR       0x000 // Data register
#define PL011_FR       0x018 // Flag register
#define PL011_FR_RXFE  BIT(4) // Receive FIFO empty
#define PL011_FR_TXFE  BIT(7) // Transmit FIFO empty

// PrimeCell / peripheral identification (standard PL011 values), 0xfe0..0xffc.
static const u8 pl011_id[8] = {0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1};

// Register access at page offset `off` (0..0xfff). Called from handle_vuart for the
// second page of the UART block. Returns true (always handled).
bool pl011_reg(u64 off, u64 *val, bool write)
{
    // One-shot: proves the DBG2 -> BCD -> PL011 chain is live (kdcom touched us).
    static bool announced = false;
    if (!announced) {
        announced = true;
        printf("HV: PL011 first access (kd debug port live) off=0x%lx write=%d\n", off, write);
    }

    if (write) {
        if (off == PL011_DR) {
            u8 b = *val;
            if (iodev_can_write(IODEV_USB_VUART))
                iodev_write(IODEV_USB_VUART, &b, 1);
        }
        // Baud/line-control/interrupt registers: accepted, no effect.
        return true;
    }

    switch (off) {
        case PL011_DR: {
            u8 c = 0;
            if (iodev_can_read(IODEV_USB_VUART))
                iodev_read(IODEV_USB_VUART, &c, 1);
            *val = c;
            break;
        }
        case PL011_FR:
            *val = PL011_FR_TXFE; // TX always ready
            if (!iodev_can_read(IODEV_USB_VUART))
                *val |= PL011_FR_RXFE;
            break;
        default:
            if (off >= 0xfe0 && off <= 0xffc)
                *val = pl011_id[(off - 0xfe0) >> 2];
            else
                *val = 0;
            break;
    }
    return true;
}
