// SPDX-License-Identifier: GPL-3.0-or-later
#include "port_supervisor.hpp"
#include "port_safety.hpp"

#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

namespace {

constexpr const char* kDaemonCrashLog = "/data/etaHEN/experimental-daemon-crash.log";
constexpr const char* kUtilityCrashLog = "/data/etaHEN/experimental-utility-crash.log";

long file_signature(const char* path) {
  struct stat st{};
  if (stat(path, &st) != 0) return -1;
  return (long)(st.st_size ^ (st.st_mtime << 8));
}

void* supervisor_thread(void*) {
  long daemon = file_signature(kDaemonCrashLog);
  long utility = file_signature(kUtilityCrashLog);

  for (;;) {
    port_safety::heartbeat();

    const long daemon_now = file_signature(kDaemonCrashLog);
    if (daemon_now >= 0 && daemon_now != daemon) {
      daemon = daemon_now;
      port_safety::note_crash("daemon", "new daemon crash log");
    }

    const long utility_now = file_signature(kUtilityCrashLog);
    if (utility_now >= 0 && utility_now != utility) {
      utility = utility_now;
      port_safety::note_crash("utility", "new utility crash log");
    }

    sleep(3);
  }

  return nullptr;
}

}

void port_supervisor_start() {
  static bool started = false;
  if (started) return;
  started = true;

  pthread_t thread{};
  if (pthread_create(&thread, nullptr, supervisor_thread, nullptr) == 0) {
    pthread_detach(thread);
    port_safety::append_log(port_safety::kCrashLog, "supervisor", "safety supervisor started");
  }
}
