#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_autonomous_boot.h"

struct fake_io {
    uint64_t now;
    enum hv_autonomous_command command;
    unsigned services;
    unsigned launches;
    unsigned proxies;
};

static uint64_t fake_now(void *opaque)
{
    return ((struct fake_io *)opaque)->now++;
}

static enum hv_autonomous_command fake_command(void *opaque)
{
    struct fake_io *io = opaque;
    enum hv_autonomous_command command = io->command;
    io->command = HV_AUTONOMOUS_COMMAND_NONE;
    return command;
}

static void fake_service(void *opaque)
{
    ((struct fake_io *)opaque)->services++;
}

static bool fake_launch(const struct hv_autonomous_payload *payload,
                        struct hv_autonomous_status *status, void *opaque)
{
    struct fake_io *io = opaque;
    assert(payload != NULL);
    assert(status != NULL);
    io->launches++;
    return true;
}

static void fake_proxy(void *opaque)
{
    ((struct fake_io *)opaque)->proxies++;
}

static struct hv_autonomous_boot_ops fake_ops = {
    .now = fake_now,
    .command = fake_command,
    .service = fake_service,
    .launch = fake_launch,
    .proxy = fake_proxy,
};

int main(void)
{
    struct hv_autonomous_payload payload = {.compressed = "xz", .compressed_size = 2};
    struct hv_autonomous_status status = {0};
    struct fake_io io = {0};

    assert(hv_autonomous_boot_poll(3, &fake_ops, &payload, &status, &io) ==
           HV_AUTONOMOUS_BOOT_LAUNCHED);
    assert(io.launches == 1 && io.proxies == 0 && io.services >= 3);

    io = (struct fake_io){.command = HV_AUTONOMOUS_COMMAND_PROXY};
    assert(hv_autonomous_boot_poll(30, &fake_ops, &payload, &status, &io) ==
           HV_AUTONOMOUS_BOOT_PROXY);
    assert(io.launches == 0 && io.proxies == 1);

    io = (struct fake_io){.command = HV_AUTONOMOUS_COMMAND_HOLD};
    assert(hv_autonomous_boot_poll(3, &fake_ops, &payload, &status, &io) ==
           HV_AUTONOMOUS_BOOT_HELD);
    assert(io.launches == 0 && io.proxies == 0);

    io.command = HV_AUTONOMOUS_COMMAND_RELEASE;
    assert(hv_autonomous_boot_poll(3, &fake_ops, &payload, &status, &io) ==
           HV_AUTONOMOUS_BOOT_LAUNCHED);
    assert(io.launches == 1);

    io = (struct fake_io){0};
    assert(hv_autonomous_boot_poll(3, &fake_ops, NULL, &status, &io) ==
           HV_AUTONOMOUS_BOOT_INVALID);
    assert(io.launches == 0 && io.proxies == 0);

    puts("hv_autonomous_boot_test: ok");
    return 0;
}
