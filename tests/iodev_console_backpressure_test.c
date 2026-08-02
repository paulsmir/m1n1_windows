#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/* Compile the real iodev.c with host-side definitions for its tiny platform surface. */
#define IODEV_H
#define MEMORY_H
#define STRING_H
#define BIT(n)              (1UL << (n))
#define min(a, b)           ((a) < (b) ? (a) : (b))
#define max(a, b)           ((a) > (b) ? (a) : (b))
#define ALIGNED(x)          __attribute__((aligned(x)))
#define SPINLOCK_ALIGN      64
#define SPINLOCK_INIT       {-1, 0}
#define DECLARE_SPINLOCK(n) spinlock_t n = SPINLOCK_INIT

typedef int64_t s64;
typedef uint8_t u8;
typedef struct {
    s64 lock;
    int count;
} spinlock_t ALIGNED(SPINLOCK_ALIGN);

typedef enum {
    IODEV_UART,
    IODEV_FB,
    IODEV_USB_VUART,
    IODEV_USB0,
    IODEV_MAX = IODEV_USB0 + 8,
    IODEV_LOG = IODEV_MAX,
    IODEV_NUM,
} iodev_id_t;

typedef enum {
    USAGE_CONSOLE = BIT(0),
    USAGE_UARTPROXY = BIT(1),
} iodev_usage_t;

struct iodev_ops {
    ssize_t (*can_read)(void *opaque);
    bool (*can_write)(void *opaque);
    size_t (*write_space)(void *opaque);
    ssize_t (*read)(void *opaque, void *buf, size_t length);
    ssize_t (*write)(void *opaque, const void *buf, size_t length);
    ssize_t (*queue)(void *opaque, const void *buf, size_t length);
    void (*flush)(void *opaque);
    void (*handle_events)(void *opaque);
};

struct iodev_iovec {
    const void *data;
    size_t length;
};

struct iodev {
    const struct iodev_ops *ops;
    spinlock_t lock;
    iodev_usage_t usage;
    void *opaque;
};

static bool mmu_active(void)
{
    return false;
}

static bool is_boot_cpu(void)
{
    return true;
}

static void spin_lock(spinlock_t *lock)
{
    (void)lock;
}

static bool spin_try_lock(spinlock_t *lock)
{
    (void)lock;
    return true;
}

static void spin_unlock(spinlock_t *lock)
{
    (void)lock;
}

#include "../src/iodev.c"

struct iodev iodev_uart;
struct iodev iodev_fb;
struct iodev iodev_log;
struct iodev iodev_usb_vuart;

struct fake_device {
    size_t free;
    unsigned data_write_calls;
    unsigned queue_calls;
    size_t bytes_written;
};

static bool fake_can_write(void *opaque)
{
    (void)opaque;
    return true;
}

static size_t fake_write_space(void *opaque)
{
    return ((struct fake_device *)opaque)->free;
}

static ssize_t fake_write(void *opaque, const void *buf, size_t length)
{
    struct fake_device *fake = opaque;
    (void)buf;
    if (length)
        fake->data_write_calls++;
    if (length > fake->free)
        return 0;
    fake->free -= length;
    fake->bytes_written += length;
    return (ssize_t)length;
}

static ssize_t fake_queue(void *opaque, const void *buf, size_t length)
{
    struct fake_device *fake = opaque;
    (void)buf;
    fake->queue_calls++;
    if (length > fake->free)
        return 0;
    fake->free -= length;
    fake->bytes_written += length;
    return (ssize_t)length;
}

int main(void)
{
    static const struct iodev_ops ops = {
        .can_write = fake_can_write,
        .write_space = fake_write_space,
        .write = fake_write,
        .queue = fake_queue,
    };
    struct fake_device fake = {.free = 0};
    struct iodev device = {
        .ops = &ops,
        .usage = USAGE_CONSOLE,
        .opaque = &fake,
    };

    iodevs[IODEV_USB0] = &device;
    iodev_console_write("blocked", 7);

    /* A full asynchronous USB ring must never enter its potentially blocking write(). */
    assert(fake.data_write_calls == 0);
    assert(fake.queue_calls == 0);

    fake.free = 64;
    iodev_console_write(NULL, 0);
    assert(fake.data_write_calls == 0);
    assert(fake.queue_calls == 1);
    assert(fake.bytes_written == 7);

    puts("iodev_console_backpressure_test: ok");
    return 0;
}
