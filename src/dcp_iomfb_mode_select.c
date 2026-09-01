/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_mode_select.h"

#include <limits.h>
#include <string.h>

#define OSSERIALIZE_MAGIC 0xd3u
#define OSSERIALIZE_LAST 0x80000000u
#define OSSERIALIZE_TYPE_SHIFT 24u
#define OSSERIALIZE_TYPE_MASK 0x1fu
#define OSSERIALIZE_SIZE_MASK 0x00ffffffu

#define OSSERIALIZE_DICTIONARY 1u
#define OSSERIALIZE_ARRAY 2u
#define OSSERIALIZE_INTEGER 4u
#define OSSERIALIZE_STRING 9u
#define OSSERIALIZE_DATA 10u
#define OSSERIALIZE_BOOLEAN 11u

#define OSSERIALIZE_INTEGER_BITS 64u
#define OSSERIALIZE_MAX_DEPTH 12u
#define OSSERIALIZE_MAX_OBJECTS 4096u

struct os_cursor {
    const uint8_t *data;
    size_t size;
    size_t offset;
    unsigned int objects;
};

struct os_tag {
    unsigned int type;
    uint32_t size;
    bool last;
};

struct mode_record {
    uint64_t id;
    uint64_t score;
    uint64_t depth;
    bool is_virtual;
    bool has_id;
    bool has_score;
    bool has_depth;
    bool has_is_virtual;
};

static bool add_size(size_t a, size_t b, size_t *result)
{
    if (b > SIZE_MAX - a)
        return false;
    *result = a + b;
    return true;
}

static bool read_bytes(struct os_cursor *cursor, size_t length,
                       const uint8_t **bytes)
{
    size_t end;

    if (!add_size(cursor->offset, length, &end) || end > cursor->size)
        return false;
    if (bytes)
        *bytes = cursor->data + cursor->offset;
    cursor->offset = end;
    return true;
}

static bool read_u32(struct os_cursor *cursor, uint32_t *value)
{
    const uint8_t *bytes;

    if (!read_bytes(cursor, sizeof(*value), &bytes))
        return false;
    memcpy(value, bytes, sizeof(*value));
    return true;
}

static bool read_u64(struct os_cursor *cursor, uint64_t *value)
{
    const uint8_t *bytes;

    if (!read_bytes(cursor, sizeof(*value), &bytes))
        return false;
    memcpy(value, bytes, sizeof(*value));
    return true;
}

static bool read_tag(struct os_cursor *cursor, struct os_tag *tag)
{
    uint32_t raw;
    size_t aligned;

    if (!add_size(cursor->offset, 3u, &aligned))
        return false;
    aligned &= ~(size_t)3u;
    if (aligned > cursor->size)
        return false;
    cursor->offset = aligned;

    if (cursor->objects >= OSSERIALIZE_MAX_OBJECTS ||
        !read_u32(cursor, &raw))
        return false;
    cursor->objects++;

    tag->last = (raw & OSSERIALIZE_LAST) != 0;
    tag->type = (raw >> OSSERIALIZE_TYPE_SHIFT) & OSSERIALIZE_TYPE_MASK;
    tag->size = raw & OSSERIALIZE_SIZE_MASK;
    return true;
}

static bool skip_object_body(struct os_cursor *cursor,
                             const struct os_tag *tag,
                             unsigned int depth);

static bool skip_container(struct os_cursor *cursor, unsigned int count,
                           bool dictionary, unsigned int depth)
{
    unsigned int total;
    unsigned int i;

    if (depth >= OSSERIALIZE_MAX_DEPTH)
        return false;
    if (dictionary && count > UINT_MAX / 2u)
        return false;
    total = dictionary ? count * 2u : count;

    for (i = 0; i < total; i++) {
        struct os_tag child;
        bool expected_last = dictionary ?
            ((i & 1u) && i == total - 1u) : i == total - 1u;

        if (!read_tag(cursor, &child) || child.last != expected_last ||
            !skip_object_body(cursor, &child, depth + 1u))
            return false;
    }
    return true;
}

static bool skip_object_body(struct os_cursor *cursor,
                             const struct os_tag *tag,
                             unsigned int depth)
{
    switch (tag->type) {
    case OSSERIALIZE_DICTIONARY:
        return skip_container(cursor, tag->size, true, depth);
    case OSSERIALIZE_ARRAY:
        return skip_container(cursor, tag->size, false, depth);
    case OSSERIALIZE_INTEGER: {
        uint64_t ignored;
        return tag->size == OSSERIALIZE_INTEGER_BITS &&
               read_u64(cursor, &ignored);
    }
    case OSSERIALIZE_STRING:
    case OSSERIALIZE_DATA:
        return read_bytes(cursor, tag->size, NULL);
    case OSSERIALIZE_BOOLEAN:
        return tag->size <= 1u;
    default:
        return false;
    }
}

static bool key_equals(const uint8_t *key, size_t length, const char *name)
{
    size_t name_length = strlen(name);
    return length == name_length && !memcmp(key, name, length);
}

static bool parse_integer(struct os_cursor *cursor, const struct os_tag *tag,
                          uint64_t *value)
{
    return tag->type == OSSERIALIZE_INTEGER &&
           tag->size == OSSERIALIZE_INTEGER_BITS && read_u64(cursor, value);
}

static bool parse_boolean(const struct os_tag *tag, bool *value)
{
    if (tag->type != OSSERIALIZE_BOOLEAN || tag->size > 1u)
        return false;
    *value = tag->size != 0;
    return true;
}

static bool parse_mode(struct os_cursor *cursor, const struct os_tag *tag,
                       bool require_depth, struct mode_record *mode)
{
    uint32_t i;

    if (tag->type != OSSERIALIZE_DICTIONARY || !tag->size ||
        tag->size > OSSERIALIZE_MAX_OBJECTS / 2u)
        return false;

    memset(mode, 0, sizeof(*mode));
    for (i = 0; i < tag->size; i++) {
        struct os_tag key_tag;
        struct os_tag value_tag;
        const uint8_t *key;

        if (!read_tag(cursor, &key_tag) || key_tag.last ||
            key_tag.type != OSSERIALIZE_STRING ||
            !read_bytes(cursor, key_tag.size, &key) ||
            !read_tag(cursor, &value_tag) ||
            value_tag.last != (i == tag->size - 1u))
            return false;

        if (key_equals(key, key_tag.size, "ID")) {
            if (mode->has_id ||
                !parse_integer(cursor, &value_tag, &mode->id))
                return false;
            mode->has_id = true;
        } else if (key_equals(key, key_tag.size, "Score")) {
            if (mode->has_score ||
                !parse_integer(cursor, &value_tag, &mode->score))
                return false;
            mode->has_score = true;
        } else if (key_equals(key, key_tag.size, "IsVirtual")) {
            if (mode->has_is_virtual ||
                !parse_boolean(&value_tag, &mode->is_virtual))
                return false;
            mode->has_is_virtual = true;
        } else if (key_equals(key, key_tag.size, "Depth")) {
            if (mode->has_depth ||
                !parse_integer(cursor, &value_tag, &mode->depth))
                return false;
            mode->has_depth = true;
        } else if (!skip_object_body(cursor, &value_tag, 1u)) {
            return false;
        }
    }

    return mode->has_id && mode->has_score && mode->has_is_virtual &&
           (!require_depth || mode->has_depth) && mode->id <= UINT32_MAX;
}

static bool select_mode(const void *blob, size_t size, bool require_depth,
                        uint32_t *selected_id)
{
    struct os_cursor cursor = {.data = blob, .size = size};
    struct os_tag root;
    uint64_t best_score = 0;
    bool found = false;
    uint32_t magic;
    uint32_t i;

    if (!blob || !selected_id || !read_u32(&cursor, &magic) ||
        magic != OSSERIALIZE_MAGIC || !read_tag(&cursor, &root) ||
        root.type != OSSERIALIZE_ARRAY || !root.last || !root.size ||
        root.size > OSSERIALIZE_MAX_OBJECTS)
        return false;

    for (i = 0; i < root.size; i++) {
        struct mode_record mode;
        struct os_tag item;

        if (!read_tag(&cursor, &item) || item.last != (i == root.size - 1u) ||
            !parse_mode(&cursor, &item, require_depth, &mode))
            return false;
        if (mode.is_virtual || (require_depth && mode.depth != 8u))
            continue;
        if (!found || mode.score > best_score) {
            found = true;
            best_score = mode.score;
            *selected_id = (uint32_t)mode.id;
        }
    }

    if (!found)
        return false;
    while (cursor.offset < cursor.size) {
        if (cursor.data[cursor.offset++] != 0)
            return false;
    }
    return true;
}

bool dcp_iomfb_select_modes(const void *color_blob, size_t color_size,
                            const void *timing_blob, size_t timing_size,
                            struct dcp_iomfb_mode_choice *choice)
{
    struct dcp_iomfb_mode_choice selected;

    if (!choice ||
        !select_mode(color_blob, color_size, true,
                     &selected.color_mode_id) ||
        !select_mode(timing_blob, timing_size, false,
                     &selected.timing_mode_id))
        return false;
    *choice = selected;
    return true;
}
