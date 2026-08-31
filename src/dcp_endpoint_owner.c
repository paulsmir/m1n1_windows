/* SPDX-License-Identifier: MIT */

#include "dcp_endpoint_owner.h"

int dcp_endpoint_owner_shutdown(void *owner,
                                dcp_endpoint_shutdown_fn shutdown_endpoint,
                                dcp_endpoint_release_fn release_owner)
{
    int ret;

    if (!owner)
        return 0;
    if (!shutdown_endpoint || !release_owner)
        return -1;
    ret = shutdown_endpoint(owner);
    if (ret < 0)
        return ret;
    release_owner(owner);
    return 0;
}
