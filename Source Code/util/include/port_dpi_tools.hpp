// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <microhttpd.h>

bool port_dpi_tools_handle(struct MHD_Connection* conn, const char* url, const char* method);
