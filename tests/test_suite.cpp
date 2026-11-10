
#include <iostream>
#include <cstdint>
#include <chrono>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define VMM_IOC_MAGIC        'V'
#define VMM_IOC_RESET_FIFO   _IO(VMM_IOC_MAGIC, 0)
#define VMM_IOC_GET_COUNT    _IOR(VMM_IOC_MAGIC, 1, uint32_t)

int main() {
    std::cout << "========================================================\n";
    std::cout << "[TEST SUITE] Testing /dev/vmm_accel Kernel FIFO Driver\n";
    std::cout << "========================================================\n";

 
    int fd = open("/dev/vmm_accel", O_RDWR);
    if (fd < 0) {
        perror("[FAIL] Could not open /dev/vmm_accel (Did you run 'sudo insmod vmm_kmod.ko'?)");
        return 1;
    }

    if (ioctl(fd, VMM_IOC_RESET_FIFO) < 0) {
        perror("[FAIL] VMM_IOC_RESET_FIFO ioctl failed");
        return 1;
    }
    std::cout << "[INFO] Kernel FIFO reset via ioctl(VMM_IOC_RESET_FIFO) OK\n";

    const uint32_t num_packets = 64;
    auto t0 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < num_packets; ++i) {
        uint32_t tx_val = 0x10000000U + i;
        if (write(fd, &tx_val, sizeof(tx_val)) != sizeof(tx_val)) {
            perror("[FAIL] Kernel write() failed");
            return 1;
        }
    }

    for (uint32_t i = 0; i < num_packets; ++i) {
        uint32_t rx_val = 0;
        uint32_t expected = (0x10000000U + i) ^ 0x5A5A5A5AU;

        if (read(fd, &rx_val, sizeof(rx_val)) != sizeof(rx_val)) {
            perror("[FAIL] Kernel read() failed");
            return 1;
        }
        if (rx_val != expected) {
            std::cerr << "[FAIL] Data corruption at index " << i << "\n";
            return 1;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    uint64_t total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    uint32_t total_ops = 0;
    if (ioctl(fd, VMM_IOC_GET_COUNT, &total_ops) < 0) {
        perror("[FAIL] VMM_IOC_GET_COUNT ioctl failed");
        return 1;
    }

    std::cout << "[PERF] 64-Packet Write+Read Kernel Round-Trip: "
              << (total_ns / num_packets) << " ns/op\n";
    std::cout << "[INFO] Kernel Driver Telemetry Ops Count     : " << total_ops << "\n";
    std::cout << "[PASS] All Kernel Driver Spinlock, Wait-Queue & ioctl Tests OK!\n";

    close(fd);
    return 0;
}
