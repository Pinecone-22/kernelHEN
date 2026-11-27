# Kernel mode, safety and update channel

## Default mode

etaHEN still starts as userland services. The bootstrapper, critical daemon and
utility daemon are ELF payloads with kernel read/write access. KStuff remains the
kernel-hook component. This is intentional: the UI, network, FTP, DPI, plugins
and filesystem code depend on libc/pthreads/sockets.

## Opt-in kernel mode

Toolbox: **Settings -> Kernel mode on next boot**.

The switch writes `/data/etaHEN/kernel_mode_next` (and the ShellUI view under
`/user/data/etaHEN/`). On the next boot the bootstrapper:

1. consumes the one-shot flag;
2. starts the embedded `Byepervisor-Reborn` payload;
3. lets Reborn install the S3 `startup_mp` hook and enter rest mode;
4. resumes, waits for `/data/etaHENbin/hypervisor_ready`;
5. marks `/data/etaHENbin/kernel_mode_active` and continues the normal
   services with kernel text writable/XOM defeated;
6. falls back to userland mode if the payload is missing, unsupported or times
   out.

Requirements and limits:

- Byepervisor-Reborn upstream supports 13.00, 13.20 and 13.60. Other firmware
  is rejected before rest mode.
- Enable **Settings -> System -> Power Saving -> Features Available in Rest
  Mode -> Supply Power to USB Ports = Always**.
- Keep the console on AC power. A failed S3 resume can power off the console.
- The Byepervisor source is vendored under
  `Source Code/bootstrapper/ByepervisorReborn/`.
- Build the asset with `node scripts/build-byepervisor-reborn.mjs`. Without the
  asset the bootstrapper embeds a zero-length symbol and kernel mode reports
  "not built" instead of attempting anything.
- All kernel patches are runtime-only and are not written to disk. Reboot
  clears them.

This is a hybrid kernel path: the high-risk hypervisor transition runs in the
kernel/hypervisor boundary, while the existing daemons remain userland clients.
It is not a claim that the complete POSIX/C++ service stack was rewritten as a
kernel module.

## Active safety net

Every boot increments `/data/etaHENbin/boot.state`. A critical daemon marks the
boot healthy only after services are up; a crash loop enters safe mode after
three failed starts. Safe mode:

- skips plugin auto-start;
- disables Toolbox auto-start and PS5Debug;
- writes `/data/etaHEN/no_plugins`;
- is cleared by the next healthy daemon start.

Additional guards:

- `port_kernel_guard.hpp` refuses kernel reads/writes to null, userland or
  absurdly large ranges before they reach `kernel_copyin/out`.
- Plugins that fail to load are quarantined in
  `/data/etaHEN/plugin-quarantine.txt`.
- The utility supervisor records new crash logs and enables safe mode after
  repeated failures.
- Updates are staged with an ELF check and optional SHA-256; the previous ELF is
  kept as `/data/etaHENbin/etaHEN.previous.elf`. A pending update is rolled back
  if the next boot fails twice.
- Third-party code cannot make kernel panic recovery possible: a panic is
  terminal. The guard/watchdog design reduces the chance of triggering one and
  recovers from hangs/crash loops, but it is not a zero-panic guarantee.

## Self-copy

On first boot `/data/etaHEN/etaHEN.bin` (the current bootstrapper image) is
atomically copied to `/data/etaHENbin/etaHEN.elf` and `etaHEN.bin`. The updater
and recovery code use that stable copy.

## Updater and file manager

DPIv2 is enabled by default and serves `http://<PS5_IP>:12800/`.

Open `/tools` for:

- update check against `Pinecone-22/etaHEN-`;
- update download/verify/install through `update.json` or the GitHub release API;
- PKG file list, install, rename, delete and download for
  `/data/etaHEN/pkgs`, `/user/data/tmp`, `/data/etaHEN/updates` and
  `/data/etaHENbin`.
