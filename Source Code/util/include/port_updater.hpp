// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#define ETAHEN_LOCAL_VERSION "2.5B-unified-r4-safety"

struct PortUpdateInfo {
  std::string local_version = ETAHEN_LOCAL_VERSION;
  std::string version;
  std::string url;
  std::string notes;
  std::string sha256;
  long long size = 0;
  bool available = false;
};

bool port_update_check(PortUpdateInfo& info, std::string& error);
bool port_update_install(const PortUpdateInfo& info, std::string& error);
