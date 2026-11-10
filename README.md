# Nano-VMM: Lightweight KVM Hypervisor, Bare-Metal ARMv8 Guest & Kernel Driver Suite

A modular systems engineering and virtualization testbed exploring hardware-accelerated virtualization (`/dev/kvm`), bare-metal ARMv8-A (`AArch64`) exception handling under `QEMU`, and Linux kernel character device driver synchronization.

## Architecture & Modules

### 1. `guest-armv8/` — Bare-Metal ARMv8-A (`AArch64`) Guest Micro-Kernel
* **Boot & Exception Vector Table (`boot.S`, `linker.ld`):** Custom bare-metal linker script mapping DRAM to `0x40000000` on QEMU `virt` (`cortex-a72`). Configures `EL1` stack, parks secondary SMP cores via `MPIDR_EL1`, zeroes `.bss`, and installs a 2KB-aligned `VBAR_EL1` exception vector table.
* **Synchronous Trap Handling & MMIO (`kernel.c`):** Saves caller context onto the `EL1` stack, decodes `ESR_EL1` (Exception Class & ISS syndrome) and `ELR_EL1` on `SVC` traps, drives the memory-mapped PL011 UART (`0x09000000`), and benchmarks trap overhead using the ARMv8 physical counter (`CNTPCT_EL0`).
* **Debugging:** Supports remote hardware stub debugging via `qemu-system-aarch64 -S -s` and `gdb`.

### 2. `host-kvm/` — C++17 Linux `/dev/kvm` Virtual Machine Monitor (VMM)
* Allocates guest physical memory via `mmap` (`KVM_SET_USER_MEMORY_REGION`), initializes vCPU state, and runs a `KVM_RUN` execution loop.
* Traps and emulates **Memory-Mapped I/O (`KVM_EXIT_MMIO`)** reads/writes to unmapped guest physical address `0x8000` alongside virtual UART I/O ports (`KVM_EXIT_IO`).
* **Measured Performance:** `~7.1 µs` (`7,099 ns`) average guest-to-host VM-exit round-trip latency across 12 exits.

### 3. `kernel-driver/` & `tests/` — Linux Kernel Character Device Driver (`/dev/vmm_accel`)
* Loadable Linux kernel module (`vmm_kmod.c`) implementing a 64-entry virtual hardware FIFO protected by `spin_lock_irqsave` and blocking `wait_queue_head_t` synchronization, with custom `ioctl` control paths (`VMM_IOC_RESET_FIFO`, `VMM_IOC_GET_COUNT`).
* Verified via an automated C++17 test and latency benchmark harness (`tests/test_suite.cpp`), achieving **`456 ns/op`** write+read kernel round-trip latency.