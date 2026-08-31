/* SPDX-License-Identifier: MIT */
#ifndef DCP_ENDPOINT_OWNER_H
#define DCP_ENDPOINT_OWNER_H

typedef int (*dcp_endpoint_shutdown_fn)(void *owner);
typedef void (*dcp_endpoint_release_fn)(void *owner);

int dcp_endpoint_owner_shutdown(void *owner,
                                dcp_endpoint_shutdown_fn shutdown_endpoint,
                                dcp_endpoint_release_fn release_owner);

#endif
