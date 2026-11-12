#include <iostream>
#include <cstdint>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define VMM_IOC_MAGIC        'V'
#define VMM_IOC_RESET_FIFO   _IO(VMM_IOC_MAGIC, 0)
#define VMM_IOC_GET_COUNT    _IOR(VMM_IOC_MAGIC, 1, uint32_t)
#define VMM_IOC_FLIP_FB      _IOR(VMM_IOC_MAGIC, 2, uint32_t)
#define VMM_IOC_GET_FB_HASH  _IOR(VMM_IOC_MAGIC, 3, uint32_t)

int main() {
    std::cout << "Testing /dev/vmm_accel FIFO & Framebuffer\n";

    int fd = open("/dev/vmm_accel", O_RDWR);
    if (fd < 0) {
        perror("[FAIL] Could not open /dev/vmm_accel");
        return 1;
    }

    ioctl(fd, VMM_IOC_RESET_FIFO);

    const uint32_t num_packets = 64;
    auto t0 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < num_packets; ++i) {
        uint32_t tx_val = 0x10000000U + i;
        if (write(fd, &tx_val, sizeof(tx_val)) != sizeof(tx_val)) return 1;
    }
    for (uint32_t i = 0; i < num_packets; ++i) {
        uint32_t rx_val = 0;
        if (read(fd, &rx_val, sizeof(rx_val)) != sizeof(rx_val)) return 1;
        if (rx_val != ((0x10000000U + i) ^ 0x5A5A5A5AU)) return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    uint64_t fifo_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    std::cout << "[PERF] 64-Packet FIFO Write+Read Round-Trip : " << (fifo_ns / num_packets) << " ns/op\n";

    const size_t page_size = 4096;
    uint8_t *fb_base = static_cast<uint8_t *>(
        mmap(nullptr, page_size * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
    );
    if (fb_base == MAP_FAILED) {
        perror("[FAIL] mmap virtual framebuffer failed");
        return 1;
    }

    uint32_t *back_buf = reinterpret_cast<uint32_t *>(fb_base + page_size);
    uint32_t expected_hash = 0;
    auto fb_t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < (page_size / sizeof(uint32_t)); ++i) {
        uint32_t pixel = 0xFF00AA00U ^ static_cast<uint32_t>(i * 0x010101U);
        back_buf[i] = pixel;
        expected_hash ^= pixel;
    }

    uint32_t active_buf = 0;
    if (ioctl(fd, VMM_IOC_FLIP_FB, &active_buf) < 0) {
        perror("[FAIL] VMM_IOC_FLIP_FB failed");
        return 1;
    }
    auto fb_t1 = std::chrono::high_resolution_clock::now();
    uint64_t flip_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(fb_t1 - fb_t0).count();

    uint32_t kernel_fb_hash = 0;
    ioctl(fd, VMM_IOC_GET_FB_HASH, &kernel_fb_hash);

    std::cout << "[PERF] Zero-Copy 1024-Pixel Render + VSync Flip: " << flip_ns << " ns\n";
    if (active_buf == 1 && kernel_fb_hash == expected_hash) {
        std::cout << "[PASS] Zero-Copy mmap Framebuffer & Page-Flip ioctl Verified (Hash=0x"
                  << std::hex << kernel_fb_hash << std::dec << ")\n";
    } else {
        std::cerr << "[FAIL] Framebuffer hash mismatch!\n";
        return 1;
    }

    munmap(fb_base, page_size * 2);
    close(fd);
    return 0;
}