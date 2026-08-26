#include "../src/proxy_boot_identity.h"

#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

int main(void)
{
    CHECK(proxy_boot_identity_get() == 0);

    proxy_boot_identity_init(0x123456789abcdef0ULL);
    CHECK(proxy_boot_identity_get() == 0x123456789abcdef0ULL);

    /* The identity is immutable for the lifetime of one m1n1 boot. */
    proxy_boot_identity_init(0xfedcba9876543210ULL);
    CHECK(proxy_boot_identity_get() == 0x123456789abcdef0ULL);
    return 0;
}
