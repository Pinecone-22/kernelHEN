// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>

namespace port_safety {

inline constexpr const char* kRoot = "/data/etaHEN";
inline constexpr const char* kBinRoot = "/data/etaHENbin";
inline constexpr const char* kEtahenElf = "/data/etaHENbin/etaHEN.elf";
inline constexpr const char* kEtahenBin = "/data/etaHENbin/etaHEN.bin";
inline constexpr const char* kPreviousElf = "/data/etaHENbin/etaHEN.previous.elf";
inline constexpr const char* kBootState = "/data/etaHENbin/boot.state";
inline constexpr const char* kSafeMode = "/data/etaHEN/safe_mode";
inline constexpr const char* kSafeModeManual = "/data/etaHEN/safe_mode.manual";
inline constexpr const char* kNoPlugins = "/data/etaHEN/no_plugins";
inline constexpr const char* kKernelModeNext = "/data/etaHEN/kernel_mode_next";
inline constexpr const char* kKernelModeNextShell = "/user/data/etaHEN/kernel_mode_next";
inline constexpr const char* kKernelModeActive = "/data/etaHENbin/kernel_mode_active";
inline constexpr const char* kUpdatePending = "/data/etaHENbin/update.pending";
inline constexpr const char* kCrashLog = "/data/etaHEN/safety-crash.log";
inline constexpr const char* kCrashCount = "/data/etaHENbin/crash.count";
inline constexpr const char* kPluginQuarantine = "/data/etaHEN/plugin-quarantine.txt";
inline constexpr const char* kHeartbeat = "/system_tmp/etahen-heartbeat";

inline bool exists(const char* path) {
  struct stat st{};
  return path && stat(path, &st) == 0;
}

inline long file_size(const char* path) {
  struct stat st{};
  return path && stat(path, &st) == 0 ? static_cast<long>(st.st_size) : -1;
}

inline bool mkdir_p(const char* path) {
  if (!path || !*path) return false;
  if (mkdir(path, 0777) == 0 || exists(path)) return true;
  return false;
}

inline bool read_text(const char* path, std::string& out) {
  out.clear();
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;

  char buffer[512];
  ssize_t n;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    out.append(buffer, static_cast<size_t>(n));
    if (out.size() > 65536) break;
  }
  close(fd);
  return true;
}

inline bool write_all(int fd, const void* data, size_t length) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (length) {
    ssize_t n = write(fd, p, length);
    if (n <= 0) return false;
    p += n;
    length -= static_cast<size_t>(n);
  }
  return true;
}

inline bool write_binary_atomic(const char* path, const void* data, size_t length, mode_t mode) {
  if (!path || !data) return false;

  std::string temp(path);
  temp += ".tmp";

  int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) return false;

  bool ok = write_all(fd, data, length);
  if (ok) ok = fsync(fd) == 0;
  close(fd);

  if (!ok) {
    unlink(temp.c_str());
    return false;
  }

  if (rename(temp.c_str(), path) != 0) {
    unlink(temp.c_str());
    return false;
  }

  chmod(path, mode);
  return true;
}

inline bool write_text_atomic(const char* path, const std::string& text) {
  return write_binary_atomic(path, text.data(), text.size(), 0644);
}

inline bool is_elf(const uint8_t* data, size_t length) {
  return data && length >= 64 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

inline bool copy_file_atomic(const char* src, const char* dst, bool require_elf, bool force) {
  if (!exists(src)) return false;
  if (!force && exists(dst) && file_size(dst) == file_size(src)) return true;

  int in = open(src, O_RDONLY);
  if (in < 0) return false;

  struct stat st{};
  if (fstat(in, &st) != 0 || st.st_size < 64 || st.st_size > (256L * 1024 * 1024)) {
    close(in);
    return false;
  }

  const size_t size = static_cast<size_t>(st.st_size);
  uint8_t* data = static_cast<uint8_t*>(malloc(size));
  if (!data) {
    close(in);
    return false;
  }

  size_t offset = 0;
  while (offset < size) {
    ssize_t n = read(in, data + offset, size - offset);
    if (n <= 0) break;
    offset += static_cast<size_t>(n);
  }
  close(in);

  bool ok = offset == size && (!require_elf || is_elf(data, size));
  if (ok) ok = write_binary_atomic(dst, data, size, 0755);
  free(data);
  return ok;
}

inline std::string timestamp() {
  char buffer[64]{};
  time_t now = time(nullptr);
  struct tm tmv{};
  localtime_r(&now, &tmv);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmv);
  return buffer;
}

inline void append_log(const char* path, const char* component, const char* message) {
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) return;

  std::string line = timestamp();
  line += " [";
  line += component ? component : "safety";
  line += "] ";
  line += message ? message : "";
  line += "\n";
  write_all(fd, line.data(), line.size());
  close(fd);
}

inline int read_int_file(const char* path, int fallback = 0) {
  std::string text;
  if (!read_text(path, text)) return fallback;
  return atoi(text.c_str());
}

inline void write_int_file(const char* path, int value) {
  write_text_atomic(path, std::to_string(value));
}

inline void touch(const char* path) {
  int fd = open(path, O_WRONLY | O_CREAT, 0644);
  if (fd >= 0) close(fd);
}

inline bool safe_mode() {
  return exists(kSafeMode) || exists(kSafeModeManual);
}

inline bool skip_plugins() {
  return safe_mode() || exists(kNoPlugins);
}

inline bool kernel_mode_requested() {
  const bool data_flag = exists(kKernelModeNext);
  const bool shell_flag = exists(kKernelModeNextShell);
  if (!data_flag && !shell_flag) return false;

  const char* path = data_flag ? kKernelModeNext : kKernelModeNextShell;
  int value = read_int_file(path, 0);
  unlink(kKernelModeNext);
  unlink(kKernelModeNextShell);
  return value != 0;
}

inline void set_kernel_mode_requested(bool enabled) {
  if (enabled) {
    write_text_atomic(kKernelModeNext, "1\n");
    mkdir_p("/user/data/etaHEN");
    write_text_atomic(kKernelModeNextShell, "1\n");
  } else {
    unlink(kKernelModeNext);
    unlink(kKernelModeNextShell);
  }
}

inline bool kernel_mode_active() {
  return exists(kKernelModeActive);
}

inline void mark_kernel_mode_active(bool active) {
  if (active) write_text_atomic(kKernelModeActive, timestamp() + "\n");
  else unlink(kKernelModeActive);
}

inline void rollback_update() {
  if (!exists(kUpdatePending) || !exists(kPreviousElf)) return;

  bool ok = copy_file_atomic(kPreviousElf, kEtahenElf, true, true);
  if (ok) ok = copy_file_atomic(kPreviousElf, kEtahenBin, true, true);
  if (ok) ok = copy_file_atomic(kPreviousElf, "/data/etaHEN/etaHEN.bin", true, true);

  append_log(kCrashLog, "update", ok ? "pending update rolled back to previous ELF" : "update rollback failed");
  touch(ok ? "/data/etaHENbin/update.rolledback" : "/data/etaHENbin/update.rollback-failed");
  unlink(kUpdatePending);
}

inline void self_copy() {
  mkdir_p(kRoot);
  mkdir_p(kBinRoot);

  if (exists(kEtahenElf) && file_size(kEtahenElf) > 0) {
    copy_file_atomic(kEtahenElf, kEtahenBin, true, false);
    return;
  }

  const char* sources[] = {
    "/data/etaHEN/etaHEN.bin",
    "/user/data/etaHEN/etaHEN.bin",
    "/proc/self/exe",
    "/proc/curproc/file",
  };

  for (const char* source : sources) {
    if (!exists(source)) continue;
    if (!copy_file_atomic(source, kEtahenElf, true, true)) continue;
    copy_file_atomic(source, kEtahenBin, true, true);
    copy_file_atomic(source, "/data/etaHEN/etaHEN.bin", true, true);
    append_log(kCrashLog, "self-copy", source);
    return;
  }

  append_log(kCrashLog, "self-copy", "no usable self image found");
}

inline void boot_begin() {
  mkdir_p(kRoot);
  mkdir_p(kBinRoot);
  mkdir_p("/system_tmp");

  int attempts = read_int_file(kBootState, 0) + 1;
  write_text_atomic(kBootState, std::to_string(attempts) + "\n" + timestamp() + "\n");

  if (exists(kUpdatePending) && attempts >= 2) {
    rollback_update();
  }

  if (attempts >= 3) {
    touch(kSafeMode);
    append_log(kCrashLog, "safety", "three failed starts; entering safe mode");
  }

  self_copy();
  touch(kHeartbeat);
}

inline void mark_boot_ok() {
  append_log(kCrashLog, "safety", "bootstrap completed; waiting for services");
}

inline void mark_healthy() {
  write_text_atomic(kBootState, "0\n" + timestamp() + "\n");
  write_int_file(kCrashCount, 0);
  unlink(kUpdatePending);
  append_log(kCrashLog, "safety", "services healthy; safe mode cleared");
  unlink(kSafeMode);
}

inline void note_crash(const char* component, const char* reason) {
  append_log(kCrashLog, component, reason);

  int crashes = read_int_file(kCrashCount, 0) + 1;
  write_int_file(kCrashCount, crashes);

  if (crashes >= 3) {
    touch(kSafeMode);
    append_log(kCrashLog, "safety", "repeated crashes; safe mode enabled");
  }
}

inline void clear_crashes() {
  write_int_file(kCrashCount, 0);
}

inline void heartbeat() {
  mkdir_p("/system_tmp");
  touch(kHeartbeat);
}

inline bool plugin_quarantined(const char* path) {
  std::string text;
  if (!read_text(kPluginQuarantine, text) || !path) return false;

  std::string line;
  const size_t length = strlen(path);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    line.assign(text, start, end - start);
    if (line.size() >= length && line.compare(0, length, path) == 0) return true;
    start = end + 1;
  }
  return false;
}

inline void quarantine_plugin(const char* path, const char* reason) {
  append_log(kPluginQuarantine, "plugin", reason);
  if (!path) return;

  int fd = open(kPluginQuarantine, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) return;

  std::string line = path;
  line += " | ";
  line += reason ? reason : "quarantined";
  line += "\n";
  write_all(fd, line.data(), line.size());
  close(fd);
}

}
