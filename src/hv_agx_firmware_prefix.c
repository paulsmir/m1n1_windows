#include "hv_agx_firmware_prefix.h"
#include <string.h>

unsigned char hv_agx_firmware_prefix_read(
    unsigned long long base, unsigned long long length, unsigned long long epoch,
    hv_agx_prefix_reader reader, void *context, unsigned long long offset,
    unsigned long long *value, unsigned char write, unsigned int width)
{
    AGX_FW_PREFIX p = {AGX_FW_PREFIX_MAGIC, 1, AGX_FW_PREFIX_SIZE, 16,
                       base, length, epoch, 0, {0, 0}};
    unsigned int bytes;
    if (!value || write || width > 3)
        return 0;
    bytes = 1u << width;
    if ((offset & (bytes - 1)) || offset > sizeof(p) - bytes)
        return 0;
    if (epoch && reader && AgxFwPrefixGeometry(base, length) &&
        reader(context, base, p.Entries)) {
        p.Ready = 1;
        if (!AgxFwPrefixValid(&p, sizeof(p), epoch))
            p.Ready = 0;
    }
    if (!p.Ready)
        p.Entries[0] = p.Entries[1] = 0;
    *value = 0;
    memcpy(value, (const unsigned char *)&p + offset, bytes);
    return 1;
}
