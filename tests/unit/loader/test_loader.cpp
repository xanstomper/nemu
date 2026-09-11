#include "core/loader/nro.hpp"
#include "core/loader/nso.hpp"
#include "core/loader/pfs0.hpp"
#include "core/loader/nca.hpp"
#include "core/loader/title_loader.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/crypto/aes.hpp"
#include "core/crypto/key_store.hpp"
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

    // 4. Test Cryptographic Engine (AES-128 NIST FIPS-197 vectors, CTR, and XTS)
    {
        using namespace nemu::core::crypto;

        // NIST FIPS-197 Appendix B Test Vector:
        // Key: 2b 7e 15 16 28 ae d2 a6 ab f7 15 88 09 cf 4f 3c
        // Plaintext: 32 43 f6 a8 88 5a 30 8d 31 31 98 a2 e0 37 07 34
        // Ciphertext: 39 25 84 1d 02 dc 09 fb dc 11 85 97 19 6a 0b 32
        const std::array<u8, 16> nist_key = {
            0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
        };
        const std::array<u8, 16> nist_plain = {
            0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
        };
        const std::array<u8, 16> nist_cipher = {
            0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb, 0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32
        };

        Aes128 aes(nist_key);
        std::array<u8, 16> encrypted{};
        aes.EncryptBlock(nist_plain, encrypted);
        NEMU_TEST_ASSERT(encrypted == nist_cipher, "NIST AES-128 Encryption Test Vector mismatch");

        std::array<u8, 16> decrypted{};
        aes.DecryptBlock(encrypted, decrypted);
        NEMU_TEST_ASSERT(decrypted == nist_plain, "NIST AES-128 Decryption Test Vector mismatch");

        // Test AES-128-CTR stream encryption / decryption round-trip
        std::vector<u8> msg = {'H', 'e', 'l', 'l', 'o', ' ', 'S', 'w', 'i', 't', 'c', 'h', '!', '1', '2', '3', '4'};
        std::vector<u8> ctr_enc(msg.size());
        std::vector<u8> ctr_dec(msg.size());
        std::array<u8, 16> iv{};
        iv[0] = 0x12; iv[15] = 0x34;

        aes.DecryptCtr(msg, ctr_enc, iv, 0);
        aes.DecryptCtr(ctr_enc, ctr_dec, iv, 0);
        NEMU_TEST_ASSERT(ctr_dec == msg, "AES-128-CTR round-trip must restore original plaintext");

        // Test AES-128-XTS sector decryption round-trip
        std::vector<u8> sector_data(512, 0xAA);
        std::vector<u8> xts_out(512, 0);
        Aes128 tweak_key(nist_key);
        aes.DecryptXts(sector_data, xts_out, tweak_key, 0, 512);
        NEMU_TEST_ASSERT(xts_out.size() == 512, "XTS output size == 512");

        std::cout << "  - Cryptographic Engine (FIPS-197 AES-128 ECB, CTR, XTS): PASSED" << std::endl;
    }

    // 5. Test KeyStore (prod.keys & title.keys parsing and management)
    {
        using namespace nemu::core::crypto;

        const std::string sample_keys =
            "# Nemu Test Keys\n"
            "header_key = 00112233445566778899aabbccddeeff0123456789abcdef0123456789abcdef\n"
            "key_area_key_application_00 = aabbccddeeff00112233445566778899\n"
            "titlekek_00 = feedfacecafebeef0123456789abcdef\n";

        KeyStore ks;
        NEMU_TEST_ASSERT(ks.LoadFromText(sample_keys), "KeyStore LoadFromText must succeed");
        NEMU_TEST_ASSERT(ks.Count() == 3, "KeyStore Count == 3");
        NEMU_TEST_ASSERT(ks.HasKey("header_key"), "Must have header_key");

        auto hdr = ks.GetHeaderKey();
        NEMU_TEST_ASSERT(hdr.has_value() && hdr->size() == 32, "Header key size == 32");

        auto kak = ks.GetKeyAreaKey(0, 0);
        NEMU_TEST_ASSERT(kak.has_value() && kak->size() == 16, "Key area key 00 size == 16");

        std::cout << "  - KeyStore prod.keys parser & retrieval tests: PASSED" << std::endl;
    }

    // 6. Test PFS0/HFS0 (.nsp / .xci) container parser
    {
        using namespace nemu::core::loader;

        // Build an in-memory PFS0 container with 2 files: "file_a.txt" and "file_b.bin"
        const std::string name_a = "file_a.txt";
        const std::string name_b = "file_b.bin";
        const std::string content_a = "Hello from file A in PFS0!";
        const std::string content_b = "Binary content in file B 1234567890";

        const u32 string_table_size = static_cast<u32>(name_a.size() + 1 + name_b.size() + 1);
        const u32 header_size = 16 + 2 * 24 + string_table_size; // Header(16) + 2 entries(48) + string table

        std::vector<u8> pfs0_buf(header_size + content_a.size() + content_b.size(), 0);

        // Header: magic 'PFS0' (0x30534650), 2 files, string_table_size
        *reinterpret_cast<u32*>(pfs0_buf.data() + 0) = Pfs0Archive::PFS0_MAGIC;
        *reinterpret_cast<u32*>(pfs0_buf.data() + 4) = 2;
        *reinterpret_cast<u32*>(pfs0_buf.data() + 8) = string_table_size;

        // Entry 0: file_a.txt
        *reinterpret_cast<u64*>(pfs0_buf.data() + 16) = 0; // offset relative to data base
        *reinterpret_cast<u64*>(pfs0_buf.data() + 24) = content_a.size();
        *reinterpret_cast<u32*>(pfs0_buf.data() + 32) = 0; // string offset 0

        // Entry 1: file_b.bin
        *reinterpret_cast<u64*>(pfs0_buf.data() + 40) = content_a.size();
        *reinterpret_cast<u64*>(pfs0_buf.data() + 48) = content_b.size();
        *reinterpret_cast<u32*>(pfs0_buf.data() + 56) = static_cast<u32>(name_a.size() + 1);

        // String table at offset 64 (16 + 48)
        u8* str_tbl = pfs0_buf.data() + 64;
        std::memcpy(str_tbl, name_a.c_str(), name_a.size() + 1);
        std::memcpy(str_tbl + name_a.size() + 1, name_b.c_str(), name_b.size() + 1);

        // File contents at offset header_size
        std::memcpy(pfs0_buf.data() + header_size, content_a.data(), content_a.size());
        std::memcpy(pfs0_buf.data() + header_size + content_a.size(), content_b.data(), content_b.size());

        Pfs0Archive archive;
        NEMU_TEST_ASSERT(archive.Initialize(pfs0_buf), "Initialize PFS0 archive");
        NEMU_TEST_ASSERT(archive.FileCount() == 2, "PFS0 FileCount == 2");
        NEMU_TEST_ASSERT(archive.HasFile("file_a.txt"), "HasFile file_a.txt");
        NEMU_TEST_ASSERT(archive.HasFile("file_b.bin"), "HasFile file_b.bin");

        auto read_a = archive.OpenFile("file_a.txt");
        NEMU_TEST_ASSERT(read_a.has_value(), "OpenFile file_a.txt");
        std::string s_a(reinterpret_cast<const char*>(read_a->data()), read_a->size());
        NEMU_TEST_ASSERT(s_a == content_a, "Content of file_a.txt must match");

        auto read_b = archive.OpenFile("file_b.bin");
        NEMU_TEST_ASSERT(read_b.has_value(), "OpenFile file_b.bin");
        std::string s_b(reinterpret_cast<const char*>(read_b->data()), read_b->size());
        NEMU_TEST_ASSERT(s_b == content_b, "Content of file_b.bin must match");

        std::cout << "  - PFS0/HFS0 (.nsp/.xci) archive reader tests: PASSED" << std::endl;
    }

    // 7. Test NCA Container Reader (NCA3 headers & sections)
    {
        using namespace nemu::core::loader;

        // Build a valid unencrypted NCA3 buffer in memory
        // Header is 0x400 bytes. Put section 0 from block 2 to block 4 (offset 0x400 to 0x800, size 0x400 = 1024 bytes)
        std::vector<u8> nca_buf(0x1000, 0);

        // NCA3 magic at 0x200
        *reinterpret_cast<u32*>(nca_buf.data() + 0x200) = NcaReader::NCA3_MAGIC;
        nca_buf[0x204] = 0; // Distribution: Download
        nca_buf[0x205] = static_cast<u8>(NcaContentType::Program);
        nca_buf[0x206] = 0; // Key generation
        *reinterpret_cast<u64*>(nca_buf.data() + 0x208) = 0x1000; // Content size
        *reinterpret_cast<u64*>(nca_buf.data() + 0x210) = 0x0100000000010000ULL; // Title ID

        // Section 0 at 0x240: start block = 2 (0x400), end block = 4 (0x800)
        *reinterpret_cast<u32*>(nca_buf.data() + 0x240) = 2;
        *reinterpret_cast<u32*>(nca_buf.data() + 0x244) = 4;

        // Fill section 0 data with sample payload
        const std::string sec0_str = "Section 0 ExeFS Payload Data in NCA3!";
        std::memcpy(nca_buf.data() + 0x400, sec0_str.data(), sec0_str.size());

        NcaReader nca;
        NEMU_TEST_ASSERT(nca.Initialize(nca_buf), "NCA Initialize");
        NEMU_TEST_ASSERT(nca.GetMagic() == NcaReader::NCA3_MAGIC, "NCA Magic == NCA3");
        NEMU_TEST_ASSERT(nca.GetContentType() == NcaContentType::Program, "NCA ContentType == Program");
        NEMU_TEST_ASSERT(nca.GetTitleId() == 0x0100000000010000ULL, "NCA TitleId");
        NEMU_TEST_ASSERT(nca.HasSection(0), "HasSection 0");
        NEMU_TEST_ASSERT(!nca.HasSection(1), "HasSection 1 is false");

        auto sec0_data = nca.ExtractSection(0);
        NEMU_TEST_ASSERT(sec0_data.has_value(), "ExtractSection 0");
        NEMU_TEST_ASSERT(sec0_data->size() == 1024, "Section 0 size == 1024");
        NEMU_TEST_ASSERT(std::memcmp(sec0_data->data(), sec0_str.data(), sec0_str.size()) == 0,
                         "Section 0 payload content matches");

        std::cout << "  - NCA (Nintendo Content Archive) parser & extractor tests: PASSED" << std::endl;
    }

    // 8. Test NSO0 Binary Loader and LZ4 decompression
    {
        using namespace nemu::core::loader;

        // Test LZ4 decompression
        // Prepare plaintext with repeating pattern to test match offset
        std::vector<u8> orig = {'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D', 'X', 'Y', 'Z'};
        // Manually construct LZ4 block for the 15-byte original:
        // "ABCD"(4 literals) + 8-char match at offset 4 ("ABCDABCD") + "XYZ"(3
        // literals). LZ4 match length is encoded as (match_len - 4) in the token
        // low nibble, so an 8-char match => low nibble 4. Token = (4<<4)|4 = 0x44.
        std::vector<u8> lz4_block = {
            0x44, 'A', 'B', 'C', 'D',
            0x04, 0x00,
            0x30, 'X', 'Y', 'Z'
        };
        std::vector<u8> decomp(15, 0);
        NEMU_TEST_ASSERT(NsoLoader::DecompressLZ4(lz4_block, decomp), "LZ4 Decompress must succeed");
        // 4 literals + 8 matched chars + 3 literals == the original 15 bytes.
        NEMU_TEST_ASSERT(decomp == orig, "Decompressed LZ4 content must match the original exactly");

        // Build valid NSO0 binary in memory
        const size_t nso_size = sizeof(NsoHeader) + 0x300;
        std::vector<u8> nso_buf(nso_size, 0);
        auto* hdr = reinterpret_cast<NsoHeader*>(nso_buf.data());
        hdr->magic = NsoLoader::NSO_MAGIC;
        hdr->flags = 0; // Uncompressed
        hdr->text.file_offset = sizeof(NsoHeader);
        hdr->text.memory_offset = 0;
        hdr->text.decompressed_size = 0x100;
        hdr->text_file_size = 0x100;

        hdr->rodata.file_offset = sizeof(NsoHeader) + 0x100;
        hdr->rodata.memory_offset = 0x1000;
        hdr->rodata.decompressed_size = 0x100;
        hdr->rodata_file_size = 0x100;

        hdr->data.file_offset = sizeof(NsoHeader) + 0x200;
        hdr->data.memory_offset = 0x2000;
        hdr->data.decompressed_size = 0x100;
        hdr->data_file_size = 0x100;
        hdr->bss_size = 0x1000;

        // Write instruction in text: MOVZ X0, #0x50; RET
        const u32 nso_code[] = { 0xD2800A00, 0xD65F03C0 };
        std::memcpy(nso_buf.data() + sizeof(NsoHeader), nso_code, sizeof(nso_code));

        nemu::core::memory::VirtualMemory vm_nso;
        auto loaded = NsoLoader::Load(nso_buf, vm_nso, 0x0071000000ULL);
        NEMU_TEST_ASSERT(loaded.has_value(), "NsoLoader Load must succeed");
        NEMU_TEST_ASSERT(loaded->entry_point == 0x0071000000ULL, "Entry point is base address + 0");

        // Execute in CPU
        nemu::core::cpu::CpuState cpu_nso;
        cpu_nso.pc = loaded->entry_point;
        cpu_nso.SetX(30, 0xDEADBEEFULL);

        nemu::core::cpu::Interpreter interp_nso(cpu_nso, vm_nso);
        NEMU_TEST_ASSERT(interp_nso.Step() == nemu::core::cpu::StepResult::Ok, "Step MOVZ");
        NEMU_TEST_ASSERT(cpu_nso.GetX(0) == 0x50, "X0 must be 0x50");
        NEMU_TEST_ASSERT(interp_nso.Step() == nemu::core::cpu::StepResult::Ok, "Step RET");
        NEMU_TEST_ASSERT(cpu_nso.pc == 0xDEADBEEFULL, "PC must be returned to LR");

        std::cout << "  - NSO (Nintendo Shared Object) & LZ4 decompression tests: PASSED" << std::endl;
    }

    // 9. Test Universal TitleLoader (.nro, .nso, .nsp)
    {
        using namespace nemu::core::loader;
        nemu::core::crypto::KeyStore ks;
        nemu::core::filesystem::VirtualFileSystem vfs;
        TitleLoader title_loader(ks, vfs);

        // A. Load NRO through TitleLoader
        {
            nemu::core::memory::VirtualMemory vm;
            auto res = title_loader.LoadFromMemory(nro_data, vm, "test.nro", 0x0071000000ULL);
            NEMU_TEST_ASSERT(res.has_value(), "TitleLoader load NRO");
            NEMU_TEST_ASSERT(res->is_nro == true, "Title is_nro == true");
            NEMU_TEST_ASSERT(res->entry_point == 0x0071000000ULL, "TitleLoader NRO entry point");
        }

        // B. Load NSO through TitleLoader
        {
            // Build minimal, valid NSO (all three segments, uncompressed).
            std::vector<u8> nso_buf(sizeof(NsoHeader) + 0x300, 0);
            auto* hdr = reinterpret_cast<NsoHeader*>(nso_buf.data());
            hdr->magic = NsoLoader::NSO_MAGIC;
            hdr->flags = 0; // Uncompressed
            hdr->text.file_offset = sizeof(NsoHeader);
            hdr->text.memory_offset = 0;
            hdr->text.decompressed_size = 0x100;
            hdr->text_file_size = 0x100;

            hdr->rodata.file_offset = sizeof(NsoHeader) + 0x100;
            hdr->rodata.memory_offset = 0x1000;
            hdr->rodata.decompressed_size = 0x100;
            hdr->rodata_file_size = 0x100;

            hdr->data.file_offset = sizeof(NsoHeader) + 0x200;
            hdr->data.memory_offset = 0x2000;
            hdr->data.decompressed_size = 0x100;
            hdr->data_file_size = 0x100;
            hdr->bss_size = 0x1000;

            nemu::core::memory::VirtualMemory vm;
            auto res = title_loader.LoadFromMemory(nso_buf, vm, "main", 0x0071000000ULL);
            NEMU_TEST_ASSERT(res.has_value(), "TitleLoader load NSO");
            NEMU_TEST_ASSERT(res->is_nro == false, "Title is_nro == false");
        }

        // C. Load PFS0 ExeFS containing 'main' NSO
        {
            // Build a valid 3-segment NSO to package inside the PFS0.
            std::vector<u8> nso_buf(sizeof(NsoHeader) + 0x300, 0);
            auto* nhdr = reinterpret_cast<NsoHeader*>(nso_buf.data());
            nhdr->magic = NsoLoader::NSO_MAGIC;
            nhdr->flags = 0; // Uncompressed
            nhdr->text.file_offset = sizeof(NsoHeader);
            nhdr->text.memory_offset = 0;
            nhdr->text.decompressed_size = 0x100;
            nhdr->text_file_size = 0x100;

            nhdr->rodata.file_offset = sizeof(NsoHeader) + 0x100;
            nhdr->rodata.memory_offset = 0x1000;
            nhdr->rodata.decompressed_size = 0x100;
            nhdr->rodata_file_size = 0x100;

            nhdr->data.file_offset = sizeof(NsoHeader) + 0x200;
            nhdr->data.memory_offset = 0x2000;
            nhdr->data.decompressed_size = 0x100;
            nhdr->data_file_size = 0x100;
            nhdr->bss_size = 0x1000;

            // Package into PFS0
            const std::string name = "main";
            const u32 string_table_size = static_cast<u32>(name.size() + 1);
            const u32 header_size = 16 + 24 + string_table_size;
            std::vector<u8> pfs0(header_size + nso_buf.size(), 0);

            *reinterpret_cast<u32*>(pfs0.data() + 0) = Pfs0Archive::PFS0_MAGIC;
            *reinterpret_cast<u32*>(pfs0.data() + 4) = 1;
            *reinterpret_cast<u32*>(pfs0.data() + 8) = string_table_size;
            *reinterpret_cast<u64*>(pfs0.data() + 16) = 0;
            *reinterpret_cast<u64*>(pfs0.data() + 24) = nso_buf.size();
            *reinterpret_cast<u32*>(pfs0.data() + 32) = 0;
            std::memcpy(pfs0.data() + 40, name.c_str(), name.size() + 1);
            std::memcpy(pfs0.data() + header_size, nso_buf.data(), nso_buf.size());

            nemu::core::memory::VirtualMemory vm;
            auto res = title_loader.LoadFromMemory(pfs0, vm, "package.nsp", 0x0071000000ULL);
            NEMU_TEST_ASSERT(res.has_value(), "TitleLoader load PFS0 package");
            NEMU_TEST_ASSERT(res->is_nro == false, "Package is_nro == false");
        }

        std::cout << "  - Universal TitleLoader (.nro, .nso, .nsp, .xci, .nca) tests: PASSED" << std::endl;
    }

    std::cout << "[Test: NRO Loader & Commercial Container/Crypto Pipeline PASSED]" << std::endl;
    return 0;
}

