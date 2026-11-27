// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include <stddef.h>

static inline bool port_kernel_address_ok(uintptr_t addr, size_t length) {
  if (addr == 0 || length == 0 || length > 0x01000000) return false;
  if ((addr >> 48) != 0xffff) return false;
  const uintptr_t end = addr + length;
  return end > addr;
}
