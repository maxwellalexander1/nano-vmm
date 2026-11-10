#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

static const uint8_t guest_rom_code[] = {
    /* outputting kvm to io port */

    0xB0, 'K',  0xE6, 0xE9,   
    0xB0, 'V',  0xE6, 0xE9,   
    0xB0, 'M',  0xE6, 0xE9,  
    0xB0, ' ',  0xE6, 0xE9,  
    0xB0, 'O',  0xE6, 0xE9,   
    0xB0, 'K',  0xE6, 0xE9,   
    0xB0, '!',  0xE6, 0xE9,   
    0xB0, '\n', 0xE6, 0xE9,   
    0xBB, 0x00, 0x80,      
    0xB0, 0x2A,             
    0x88, 0x07,               

    0x8A, 0x07,               

    
    0xE6, 0xEA,               

    
    0xF4                      
};

int main() {
    std::cout << "[NANO-VMM] Init c++17 linux hypervisor using /dev/kvm\n";

    int kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm_fd < 0) {
        perror("[ERROR] Failed to open /dev/kvm (Check BIOS VT-x/AMD-V or permissions)");
        return 1;
    }

    int api_ver = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_ver != 12) {
        std::cerr << "[ERROR] Unexpected KVM API version: " << api_ver << "\n";
        return 1;
    }

    int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("[ERROR] KVM_CREATE_VM failed");
        return 1;
    }

    const size_t guest_ram_size = 4096;
    void *guest_ram = mmap(nullptr, guest_ram_size, PROT_READ | PROT_WRITE, 
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);    
        if (guest_ram == MAP_FAILED) {
        perror("[ERROR] mmap guest_ram failed");
        return 1;
    }

    std::memcpy(guest_ram, guest_rom_code, sizeof(guest_rom_code));

    struct kvm_userspace_memory_region mem_region = {};
    mem_region.slot = 0;
    mem_region.guest_phys_addr = 0x0000;
    mem_region.memory_size = guest_ram_size;
    mem_region.userspace_addr = reinterpret_cast<uint64_t>(guest_ram);

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) {
        perror("[ERROR] KVM_SET_USER_MEMORY_REGION failed");
        return 1;
    }

    int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    if (vcpu_fd < 0) {
        perror("[ERROR] KVM_CREATE_VCPU failed");
        return 1;
    }

    int vcpu_mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    struct kvm_run *run = static_cast<struct kvm_run *> (
        mmap(nullptr, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0)
    );

    struct kvm_sregs sregs;
    ioctl(vcpu_fd, KVM_GET_SREGS, &sregs);
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    ioctl(vcpu_fd, KVM_SET_SREGS, &sregs);

    struct kvm_regs regs = {};
    regs.rip = 0x0000;   
    regs.rflags = 0x2;  
    ioctl(vcpu_fd, KVM_SET_REGS, &regs);

    uint8_t virtual_accel_reg = 0;
    uint8_t verified_result   = 0;
    uint64_t total_vm_exits   = 0;
    uint64_t total_exit_ns    = 0;

    std::cout << "[VMM] Entering KVM_RUN hardware virtualization loop...\n";
    std::cout << "[GUEST UART] ";

    bool running = true;
    while (running) {
        auto t0 = std::chrono::high_resolution_clock::now();

        if (ioctl(vcpu_fd, KVM_RUN, 0) < 0) {
            perror("[ERROR] KVM_RUN failed");
            return 1;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        total_vm_exits++;
        total_exit_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

        switch (run->exit_reason) {

            case KVM_EXIT_IO: {
                uint8_t *data_ptr = reinterpret_cast<uint8_t *>(run) + run->io.data_offset;

                if (run->io.direction == KVM_EXIT_IO_OUT && run->io.port == 0xE9) {
                    std::cout << static_cast<char>(*data_ptr);
                } 
                else if (run->io.direction == KVM_EXIT_IO_OUT && run->io.port == 0xEA) {
                    verified_result = *data_ptr;
                }
                break;
            }

            case KVM_EXIT_MMIO: {
                if (run->mmio.phys_addr == 0x8000) {
                    if (run->mmio.is_write) {
                        uint8_t val = run->mmio.data[0];
                        virtual_accel_reg = val * 2;
                        std::cout << "[VMM MMIO TRAP] Guest wrote " << (int)val
                                  << " to MMIO 0x8000 -> Virtual HW computed "
                                  << (int)virtual_accel_reg << "\n";
                    } else {
                        run->mmio.data[0] = virtual_accel_reg;
                        std::cout << "[VMM MMIO TRAP] Guest read from MMIO 0x8000 <- Returning "
                                  << (int)virtual_accel_reg << "\n";
                    }
                }
                break;
            }

            case KVM_EXIT_HLT: {
                std::cout << "[VMM] Guest executed HLT instruction. Shutting down VM.\n";
                running = false;
                break;
            }

            default: {
                std::cerr << "[ERROR] Unhandled VM-Exit reason: " << run->exit_reason << "\n";
                return 1;
            }
        }
    }

    std::cout << "[PERF] Total VM-Exits Handled : " << total_vm_exits << "\n";
    std::cout << "[PERF] Avg Guest+Exit Latency : " << (total_exit_ns / total_vm_exits) << " ns\n";

    if (verified_result == 84) {
        std::cout << "[PASS] KVM Hypervisor MMIO Trap & Virtual UART Test OK (42 * 2 == 84)\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Expected MMIO result 84, got " << (int)verified_result << "\n";
        return 1;
    }
}
