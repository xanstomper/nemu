#include "core/loader/nro.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/svc.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;

int main() {
    std::cout << "[Test: NRO Loader & Homebrew Execution Baseline]" << std::endl;

    // 1. Validation tests with invalid inputs
    {
        memory::VirtualMemory mem;
        std::vector<u8> empty_buf;
        NEMU_TEST_ASSERT(!loader::NroLoader::IsValidNro(empty_buf), "Empty buffer must not be valid NRO");

        std::vector<u8> garbage(sizeof(loader::NroHeader), 0xAA);
        NEMU_TEST_ASSERT(!loader::NroLoader::IsValidNro(garbage), "Garbage buffer must not be valid NRO");
    }

    // 2. Synthesize a real valid NRO binary in memory
    // Layout:
    // 0x0000 - 0x1000: Text section (contains entry jump, NRO header, and real ARM64 code at 0x80)
    // 0x1000 - 0x2000: Rodata section (read-only constants)
    // 0x2000 - 0x3000: Data section (writable globals)
    // BSS: 0x1000 bytes
    constexpr size_t TEXT_SIZE = 0x1000;
    constexpr size_t RODATA_SIZE = 0x1000;
    constexpr size_t DATA_SIZE = 0x1000;
    constexpr size_t BSS_SIZE = 0x1000;
    constexpr size_t TOTAL_FILE_SIZE = TEXT_SIZE + RODATA_SIZE + DATA_SIZE;

    std::vector<u8> nro_data(TOTAL_FILE_SIZE, 0);

    // Entry branch instruction: B +0x80 (opcode 0x14000020 -> branch +32 words = +0x80 bytes)
    const u32 branch_to_entry = 0x14000020;
    std::memcpy(nro_data.data(), &branch_to_entry, sizeof(branch_to_entry));

    // NRO Header at offset 0
    auto* header = reinterpret_cast<loader::NroHeader*>(nro_data.data());
    header->entry_point_instruction = branch_to_entry;
    header->mod0_offset = 0;
    header->magic = loader::NroLoader::NRO_MAGIC; // 'NRO0'
    header->version = 0;
    header->size = static_cast<u32>(TOTAL_FILE_SIZE);
    header->flags = 0;

    header->text.file_offset = 0;
    header->text.size = static_cast<u32>(TEXT_SIZE);

    header->rodata.file_offset = static_cast<u32>(TEXT_SIZE);
    header->rodata.size = static_cast<u32>(RODATA_SIZE);

    header->data.file_offset = static_cast<u32>(TEXT_SIZE + RODATA_SIZE);
    header->data.size = static_cast<u32>(DATA_SIZE);

    header->bss_size = static_cast<u32>(BSS_SIZE);

    const u8 test_build_id[32] = "NEMU_TEST_BUILD_ID_0123456789AB";
    std::memcpy(header->build_id, test_build_id, 32);

    // Put ARM64 code at offset 0x80 (target of the branch instruction):
    // Instruction 1: MOVZ X0, #0x2A (42)    -> 0xD2800540
    // Instruction 2: MOVZ X1, #0x64 (100)   -> 0xD2800C81
    // Instruction 3: ADD X2, X0, X1 (142)   -> 0x8B010002
    // Instruction 4: SVC #0x07 (Exit)       -> 0xD40000E1
    const u32 code[] = {
        0xD2800540, // MOVZ X0, #0x2A
        0xD2800C81, // MOVZ X1, #0x64
        0x8B010002, // ADD X2, X0, X1
        0xD40000E1  // SVC #0x07 (svcExitProcess)
    };
    std::memcpy(nro_data.data() + 0x80, code, sizeof(code));

    // Put sample rodata string at text offset + 0x10
    const char rodata_str[] = "Nemu Switch Emulator Rodata Section";
    std::memcpy(nro_data.data() + TEXT_SIZE + 0x10, rodata_str, sizeof(rodata_str));

    // Put sample data at data offset + 0x20
    const u64 sample_data_val = 0xDEADBEEFCAFEBABEULL;
    std::memcpy(nro_data.data() + TEXT_SIZE + RODATA_SIZE + 0x20, &sample_data_val, sizeof(sample_data_val));

    NEMU_TEST_ASSERT(loader::NroLoader::IsValidNro(nro_data), "Synthesized NRO must be valid");

    // 3. Load into VirtualMemory
    memory::VirtualMemory mem;
    const vaddr_t LOAD_ADDR = 0x0071000000ULL;
    auto loaded_opt = loader::NroLoader::Load(nro_data, mem, LOAD_ADDR);
    NEMU_TEST_ASSERT(loaded_opt.has_value(), "NRO Load must succeed");

    const auto& info = *loaded_opt;
    NEMU_TEST_ASSERT(info.load_address == LOAD_ADDR, "Load address mismatch");
    NEMU_TEST_ASSERT(info.entry_point == LOAD_ADDR, "Entry point mismatch");
    NEMU_TEST_ASSERT(info.text_address == LOAD_ADDR, "Text address mismatch");
    NEMU_TEST_ASSERT(info.text_size == TEXT_SIZE, "Text size mismatch");
    NEMU_TEST_ASSERT(info.rodata_address == LOAD_ADDR + TEXT_SIZE, "Rodata address mismatch");
    NEMU_TEST_ASSERT(info.rodata_size == RODATA_SIZE, "Rodata size mismatch");
    NEMU_TEST_ASSERT(info.data_address == LOAD_ADDR + TEXT_SIZE + RODATA_SIZE, "Data address mismatch");
    NEMU_TEST_ASSERT(info.data_size == DATA_SIZE, "Data size mismatch");
    NEMU_TEST_ASSERT(info.bss_address == LOAD_ADDR + TEXT_SIZE + RODATA_SIZE + DATA_SIZE, "BSS address mismatch");
    NEMU_TEST_ASSERT(info.bss_size == BSS_SIZE, "BSS size mismatch");
    NEMU_TEST_ASSERT(std::memcmp(info.build_id.data(), test_build_id, 32) == 0, "Build ID mismatch");

    // 4. Check permissions
    auto text_perm = mem.GetPagePermissions(LOAD_ADDR);
    NEMU_TEST_ASSERT(text_perm.has_value() && *text_perm == memory::MemoryPermission::ReadExecute, "Text must be ReadExecute");

    auto rodata_perm = mem.GetPagePermissions(LOAD_ADDR + TEXT_SIZE);
    NEMU_TEST_ASSERT(rodata_perm.has_value() && *rodata_perm == memory::MemoryPermission::Read, "Rodata must be Read");

    auto data_perm = mem.GetPagePermissions(LOAD_ADDR + TEXT_SIZE + RODATA_SIZE);
    NEMU_TEST_ASSERT(data_perm.has_value() && *data_perm == memory::MemoryPermission::ReadWrite, "Data must be ReadWrite");

    auto bss_perm = mem.GetPagePermissions(LOAD_ADDR + TEXT_SIZE + RODATA_SIZE + DATA_SIZE);
    NEMU_TEST_ASSERT(bss_perm.has_value() && *bss_perm == memory::MemoryPermission::ReadWrite, "BSS must be ReadWrite");

    // 5. Verify data contents in VirtualMemory
    char read_rodata[64] = {0};
    NEMU_TEST_ASSERT(mem.ReadBlock(LOAD_ADDR + TEXT_SIZE + 0x10, read_rodata, sizeof(rodata_str)), "ReadBlock rodata");
    NEMU_TEST_ASSERT(std::strcmp(read_rodata, rodata_str) == 0, "Rodata content mismatch");

    u64 read_data_val = 0;
    NEMU_TEST_ASSERT(mem.ReadBlock(LOAD_ADDR + TEXT_SIZE + RODATA_SIZE + 0x20, &read_data_val, sizeof(read_data_val)), "ReadBlock data");
    NEMU_TEST_ASSERT(read_data_val == sample_data_val, "Data content mismatch");

    // 6. Execute loaded NRO entry point using CPU Interpreter and Horizon OS Kernel
    auto process = std::make_shared<kernel::KProcess>(1, "TestApp");
    const vaddr_t STACK_TOP = 0x0080000000ULL;
    const vaddr_t TLS_ADDR = 0x0080100000ULL;
    auto thread = std::make_shared<kernel::KThread>(100, process, 44, info.entry_point, STACK_TOP, TLS_ADDR);

    cpu::CpuState& cpu = thread->GetCpuState();
    cpu.SetX(0, 0);             // X0 = thread handle / context
    cpu.SetX(1, ~0ULL);         // X1 = libnx standalone flag

    cpu::Interpreter interp(cpu, mem);
    interp.SetSvcHandler([&](cpu::CpuState& s, u32 svc_id) {
        kernel::SvcDispatcher::Dispatch(s, *process, *thread, svc_id);
    });

    // Step 1: Entry instruction (B 0x80)
    auto res1 = interp.Step();
    NEMU_TEST_ASSERT(res1 == cpu::StepResult::Ok, "Step 1 ok");
    NEMU_TEST_ASSERT(cpu.pc == LOAD_ADDR + 0x80, "PC must have branched to 0x80");

    // Step 2: MOVZ X0, #0x2A
    auto res2 = interp.Step();
    NEMU_TEST_ASSERT(res2 == cpu::StepResult::Ok, "Step 2 ok");
    NEMU_TEST_ASSERT(cpu.GetX(0) == 42, "X0 must be 42");
    NEMU_TEST_ASSERT(cpu.pc == LOAD_ADDR + 0x84, "PC advance 4 bytes");

    // Step 3: MOVZ X1, #0x64
    auto res3 = interp.Step();
    NEMU_TEST_ASSERT(res3 == cpu::StepResult::Ok, "Step 3 ok");
    NEMU_TEST_ASSERT(cpu.GetX(1) == 100, "X1 must be 100");
    NEMU_TEST_ASSERT(cpu.pc == LOAD_ADDR + 0x88, "PC advance 4 bytes");

    // Step 4: ADD X2, X0, X1
    auto res4 = interp.Step();
    NEMU_TEST_ASSERT(res4 == cpu::StepResult::Ok, "Step 4 ok");
    NEMU_TEST_ASSERT(cpu.GetX(2) == 142, "X2 must be 142 (42 + 100)");
    NEMU_TEST_ASSERT(cpu.pc == LOAD_ADDR + 0x8C, "PC advance 4 bytes");

    // Step 5: SVC #0x07 (svcExitProcess)
    auto res5 = interp.Step();
    NEMU_TEST_ASSERT(res5 == cpu::StepResult::Svc, "Step 5 must return Svc");
    NEMU_TEST_ASSERT(process->GetState() == kernel::ProcessState::Terminated, "Process must be terminated");
    NEMU_TEST_ASSERT(thread->GetState() == kernel::ThreadState::Terminated, "Thread must be terminated");

    std::cout << "[Test: NRO Loader & Homebrew Execution Baseline PASSED]" << std::endl;
    return 0;
}

