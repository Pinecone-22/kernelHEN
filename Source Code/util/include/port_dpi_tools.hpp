// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <microhttpd.h>

#ifdef __cplusplus
extern "C" {
#endif

bool port_dpi_tools_handle(struct MHD_Connection* conn, const char* url, const char* method);

#ifdef __cplusplus
}
#endif
