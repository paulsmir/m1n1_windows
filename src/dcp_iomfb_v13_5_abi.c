/* SPDX-License-Identifier: MIT */

#include "dcp_iomfb_v13_5_abi.h"

struct dcp_iomfb_abi_entry {
    uint16_t input;
    uint16_t output;
    bool valid;
};

#define ABI(_in, _out) { .input = (_in), .output = (_out), .valid = true }

/* Pinned m1n1 V13_5 IPC schema. Keep version differences explicit; in
 * particular D454/D456, D576 and D589 must never inherit v13.3 shapes. */
static const struct dcp_iomfb_abi_entry callbacks[599] = {
    [0] = ABI(0x0, 0x4), [1] = ABI(0x0, 0x4), [2] = ABI(0x0, 0x0),
    [3] = ABI(0x4, 0x3c), [6] = ABI(0x20, 0x1c),
    [100] = ABI(0x0, 0x0), [101] = ABI(0x0, 0x4),
    [102] = ABI(0x44, 0x0), [103] = ABI(0x0, 0x0),
    [104] = ABI(0x44, 0x0), [107] = ABI(0x40, 0x0),
    [108] = ABI(0x0, 0x4), [109] = ABI(0x0, 0x4),
    [110] = ABI(0x0, 0x4), [111] = ABI(0x0, 0x4),
    [112] = ABI(0x0, 0x4), [113] = ABI(0x0, 0x4),
    [114] = ABI(0x10, 0x8), [115] = ABI(0x10, 0x4),
    [116] = ABI(0x8, 0x0), [120] = ABI(0x0, 0x4),
    [121] = ABI(0x0, 0x4), [122] = ABI(0x0, 0x4),
    [124] = ABI(0x64, 0x24), [126] = ABI(0x4, 0x4),
    [127] = ABI(0x1008, 0x4), [128] = ABI(0x40, 0x4),
    [129] = ABI(0x1c, 0x14),
    [201] = ABI(0xc, 0x14), [202] = ABI(0x1c, 0x0),
    [206] = ABI(0x0, 0x4), [207] = ABI(0x0, 0x4),
    [208] = ABI(0x4, 0x0), [209] = ABI(0x0, 0x8),
    [300] = ABI(0x10, 0x0),
    [401] = ABI(0x50, 0xc), [404] = ABI(0x48, 0x0),
    [406] = ABI(0x48, 0x0), [408] = ABI(0x8, 0x8),
    [411] = ABI(0x10, 0x1c), [413] = ABI(0x1048, 0x4),
    [414] = ABI(0x50, 0x4), [415] = ABI(0x4c, 0x4),
    [451] = ABI(0x14, 0x1c), [452] = ABI(0x18, 0x14),
    [453] = ABI(0x20, 0x10), [454] = ABI(0x8, 0x4),
    [455] = ABI(0x8, 0x4), [456] = ABI(0x4, 0x4),
    [552] = ABI(0x1044, 0x4), [561] = ABI(0x1044, 0x4),
    [563] = ABI(0x4c, 0x4), [565] = ABI(0x48, 0x4),
    [567] = ABI(0x80, 0x4), [574] = ABI(0x4, 0x4),
    [575] = ABI(0x4, 0x8), [576] = ABI(0x54, 0x4c),
    [577] = ABI(0x4, 0x0), [578] = ABI(0x4, 0x4),
    [579] = ABI(0x0, 0x0), [581] = ABI(0xc, 0x0),
    [582] = ABI(0x8, 0x4), [583] = ABI(0x18, 0x4),
    [584] = ABI(0x0, 0x0), [588] = ABI(0x0, 0x0),
    [589] = ABI(0x6f0, 0x0), [591] = ABI(0x14, 0x0),
    [592] = ABI(0x4, 0x0), [593] = ABI(0x4, 0x0),
    [594] = ABI(0x4, 0x0), [596] = ABI(0x0, 0x4),
    [597] = ABI(0x0, 0x4), [598] = ABI(0x0, 0x0),
};

static const struct dcp_iomfb_abi_entry methods[473] = {
    [0] = ABI(0x4, 0x4), [29] = ABI(0x0, 0x0),
    [131] = ABI(0x0, 0x4), [132] = ABI(0x0, 0x4),
    [373] = ABI(0x0, 0x0), [374] = ABI(0x0, 0x4),
    [401] = ABI(0x0, 0x4), [407] = ABI(0x18, 0x18),
    [408] = ABI(0x1884, 0xc), [410] = ABI(0x4, 0x4),
    [411] = ABI(0x0, 0x4), [412] = ABI(0x8, 0x4),
    [422] = ABI(0x50, 0x4), [426] = ABI(0x8, 0x8),
    [441] = ABI(0x28, 0x4), [445] = ABI(0x0, 0x4),
    [449] = ABI(0x4, 0x4), [456] = ABI(0x0, 0x0),
    [457] = ABI(0x4, 0x4), [463] = ABI(0x0, 0x4),
    [466] = ABI(0x4, 0x0), [467] = ABI(0x14, 0x14),
    [472] = ABI(0xc, 0x8),
};

static bool dcp_iomfb_abi_lookup(const struct dcp_iomfb_abi_entry *table,
                                 size_t count, unsigned int id,
                                 struct dcp_iomfb_abi_size *size)
{
    if (!size || id >= count || !table[id].valid)
        return false;
    size->input = table[id].input;
    size->output = table[id].output;
    return true;
}

bool dcp_iomfb_v13_5_callback_size(unsigned int id,
                                    struct dcp_iomfb_abi_size *size)
{
    return dcp_iomfb_abi_lookup(callbacks,
                                sizeof(callbacks) / sizeof(callbacks[0]),
                                id, size);
}

bool dcp_iomfb_v13_5_method_size(unsigned int id,
                                 struct dcp_iomfb_abi_size *size)
{
    return dcp_iomfb_abi_lookup(methods,
                                sizeof(methods) / sizeof(methods[0]),
                                id, size);
}
