/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcp_iomfb_mode_select.h"

struct writer {
    uint8_t data[1024];
    size_t size;
};

static void put_u32(struct writer *writer, uint32_t value)
{
    memcpy(writer->data + writer->size, &value, sizeof(value));
    writer->size += sizeof(value);
}

static void put_u64(struct writer *writer, uint64_t value)
{
    memcpy(writer->data + writer->size, &value, sizeof(value));
    writer->size += sizeof(value);
}

static void align4(struct writer *writer)
{
    while (writer->size & 3u)
        writer->data[writer->size++] = 0;
}

static void put_tag(struct writer *writer, unsigned int type,
                    unsigned int size, bool last)
{
    align4(writer);
    put_u32(writer, (last ? 0x80000000u : 0u) |
                        ((type & 0x1fu) << 24) | (size & 0xffffffu));
}

static void put_string(struct writer *writer, const char *value)
{
    size_t length = strlen(value);
    put_tag(writer, 9, (unsigned int)length, false);
    memcpy(writer->data + writer->size, value, length);
    writer->size += length;
}

static void put_integer(struct writer *writer, uint64_t value, bool last)
{
    put_tag(writer, 4, 64, last);
    put_u64(writer, value);
}

static void put_boolean(struct writer *writer, bool value, bool last)
{
    put_tag(writer, 11, value ? 1 : 0, last);
}

static void put_mode(struct writer *writer, uint32_t id, uint64_t score,
                     bool is_virtual, uint32_t depth, bool include_depth,
                     bool last)
{
    unsigned int fields = include_depth ? 4 : 3;

    put_tag(writer, 1, fields, last);
    put_string(writer, "ID");
    put_integer(writer, id, false);
    put_string(writer, "Score");
    put_integer(writer, score, false);
    put_string(writer, "IsVirtual");
    put_boolean(writer, is_virtual, !include_depth);
    if (include_depth) {
        put_string(writer, "Depth");
        put_integer(writer, depth, true);
    }
}

static struct writer timing_blob(void)
{
    struct writer writer = {0};

    put_u32(&writer, 0xd3);
    put_tag(&writer, 2, 3, true);
    put_mode(&writer, 3, 10, false, 0, false, false);
    put_mode(&writer, 9, 99, true, 0, false, false);
    put_mode(&writer, 7, 20, false, 0, false, true);
    return writer;
}

static struct writer color_blob(void)
{
    struct writer writer = {0};

    put_u32(&writer, 0xd3);
    put_tag(&writer, 2, 3, true);
    put_mode(&writer, 2, 100, false, 10, true, false);
    put_mode(&writer, 4, 30, false, 8, true, false);
    put_mode(&writer, 5, 99, true, 8, true, true);
    return writer;
}

int main(void)
{
    struct writer timing = timing_blob();
    struct writer color = color_blob();
    struct dcp_iomfb_mode_choice choice = {0};

    assert(dcp_iomfb_select_modes(color.data, color.size, timing.data,
                                  timing.size, &choice));
    assert(choice.color_mode_id == 4);
    assert(choice.timing_mode_id == 7);

    assert(!dcp_iomfb_select_modes(color.data, color.size - 1, timing.data,
                                   timing.size, &choice));
    color.data[0] = 0;
    assert(!dcp_iomfb_select_modes(color.data, color.size, timing.data,
                                   timing.size, &choice));

    puts("dcp_iomfb_mode_select_test: ok");
    return 0;
}
