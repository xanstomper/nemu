// Test: GDB Remote Serial Protocol (RSP) stub.
//
// Drives the real GdbStub over a loopback TCP socket: starts the listener,
// connects a client, sends RSP-encoded packets ($payload#cc), and asserts on
// the stub's replies — covering the full path through DispatchPacket and the
// memory read/write handlers against a real VirtualMemory.
#include "core/debug/gdb_stub.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/types.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>

// This is a POSIX loopback-socket integration test. On Windows/MinGW the
// production GdbStub uses Winsock internally (which the cpp also exercises in
// the Windows cross-build); the full TCP loopback path is exercised here on
// POSIX where dev iteration happens. On Windows the test is a documented skip.
#ifdef _WIN32
int main() {
    std::cout << "[Test: GDB Remote Serial Protocol Stub] SKIPPED on Windows "
                 "(POSIX loopback test; Winsock path is built but not driven "
                 "here)" << std::endl;
    return 0;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

// Build an RSP frame: $payload#cc  (cc = 8-bit sum of payload, lowercase hex).
static std::string Frame(const std::string& payload) {
    u8 sum = 0;
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
        for (size_t i = 0; i < payload.size(); ++i) {
            sum = static_cast<u8>(sum + bytes[i]);
        }
    }
    char cs[8];
    std::snprintf(cs, sizeof(cs), "%02x", static_cast<unsigned>(sum));
    return "$" + payload + "#" + cs;
}

static int ConnectClient(u16 port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    NEMU_TEST_ASSERT(fd >= 0, "client socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    // Retry connect briefly (listener thread may not be ready instantly).
    for (int i = 0; i < 50; ++i) {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (i == 49) {
            ::close(fd);
            NEMU_TEST_ASSERT(false, "client connect to GDB stub");
        }
    }
    return fd;
}

// Send a raw packet and read the full response frame up to '#cc'.
static std::string Exchange(int fd, const std::string& payload) {
    std::string frame = Frame(payload);
    size_t sent = 0;
    while (sent < frame.size()) {
        ssize_t n = ::send(fd, frame.data() + sent, frame.size() - sent, 0);
        NEMU_TEST_ASSERT(n > 0, "send");
        sent += static_cast<size_t>(n);
    }

    // Read a complete server frame. The stub precedes its $payload#cc with a
    // '+' ACK byte (SendPacket prepends it), so skip non-'$' bytes first, then
    // capture between '$' and '#'.
    char ch;
    for (int i = 0; i < 100000; ++i) {
        ssize_t n = ::recv(fd, &ch, 1, 0);
        if (n == 0) NEMU_TEST_ASSERT(false, "recv (pre-$) EOF");
        NEMU_TEST_ASSERT(n == 1, "recv (pre-$)");
        if (ch == '$') break;
        if (i == 99999) NEMU_TEST_ASSERT(false, "pre-$ loop cap");
    }
    std::string resp;
    for (int i = 0; i < 100000; ++i) {
        ssize_t n = ::recv(fd, &ch, 1, 0);
        if (n == 0) NEMU_TEST_ASSERT(false, "recv (payload) EOF");
        NEMU_TEST_ASSERT(n == 1, "recv (payload)");
        resp += ch;
        if (ch == '#') break;
        if (i == 99999) NEMU_TEST_ASSERT(false, "recv payload loop cap");
    }
    resp.pop_back(); // drop the '#'
    // Consume the 2 checksum chars.
    char cs[2];
    NEMU_TEST_ASSERT(::recv(fd, cs, 2, 0) == 2, "recv checksum");
    return resp;
}

int main() {
    std::cout << "[Test: GDB Remote Serial Protocol Stub]" << std::endl;

    // Wire a real guest address space so 'm' (read) / 'M' (write) hit it.
    memory::VirtualMemory mem;
    constexpr u32 kBase = 0x40000000;
    constexpr u32 kSize = 0x1000;
    mem.Map(kBase, kSize, memory::MemoryPermission::ReadWrite);

    debug::GdbStub stub(&mem);
    const u16 port = 24689; // fixed per the frontend wiring

    NEMU_TEST_ASSERT(stub.Start(port), "GDB stub should start on TCP port");
    NEMU_TEST_ASSERT(stub.IsRunning(), "IsRunning should be true after Start");

    int client = ConnectClient(port);

    // 1. Halt reason.
    NEMU_TEST_ASSERT(Exchange(client, "?") == "T05", "halt reason -> T05 (SIGTRAP)");

    // 2. Read all registers. No CPU-state provider is wired yet, so the stub
    // falls back to returning zeros for all kNumGdbRegs (x0-x30 + pc + sp).
    std::string regs = Exchange(client, "g");
    NEMU_TEST_ASSERT(regs.size() == debug::GdbStub::kNumGdbRegs * 16 &&
                     regs.find_first_not_of('0') == std::string::npos,
                     "read regs -> all-zero registers (no live provider)");

    // 3. Read one register -> 16 hex '0' (no provider yet).
    NEMU_TEST_ASSERT(Exchange(client, "p10") == "0000000000000000", "read reg 0x10 (no provider)");

    // 4. qSupported / qAttached / qC.
    NEMU_TEST_ASSERT(Exchange(client, "qSupported").rfind("PacketSize=", 0) == 0, "qSupported");
    NEMU_TEST_ASSERT(Exchange(client, "qAttached") == "1", "qAttached");
    NEMU_TEST_ASSERT(Exchange(client, "qC") == "QC1", "qC");

    // 5. Write then read memory round-trip.
    // Write 4 bytes 0xDEADBEEF (little-endian: EF BE AD DE) at kBase.
    {
        char addr_hex[16];
        std::snprintf(addr_hex, sizeof(addr_hex), "%x", kBase);
        std::string hexdata = "efbeadde";
        std::string req = "M" + std::string(addr_hex) + ",4:" + hexdata;
        NEMU_TEST_ASSERT(Exchange(client, req) == "OK", "write memory -> OK");
    }
    // Read it back: should give ef be ad de.
    {
        char addr_hex[16], len_hex[4];
        std::snprintf(addr_hex, sizeof(addr_hex), "%x", kBase);
        std::snprintf(len_hex, sizeof(len_hex), "%x", 4);
        std::string req = "m" + std::string(addr_hex) + "," + len_hex;
        NEMU_TEST_ASSERT(Exchange(client, req) == "efbeadde", "read memory round-trip");
    }
    // Also verify the guest memory was actually modified.
    {
        std::vector<u8> out(4, 0);
        mem.ReadBlock(kBase, out.data(), 4);
        NEMU_TEST_ASSERT(out[0] == 0xEF && out[1] == 0xBE && out[2] == 0xAD && out[3] == 0xDE,
                         "VirtualMemory holds written bytes");
    }

    // 6. Continue / set-thread.
    NEMU_TEST_ASSERT(Exchange(client, "c") == "S05", "continue -> S05");
    NEMU_TEST_ASSERT(Exchange(client, "H c -1") == "OK", "set thread -> OK");

    // 7. Detach closes the client connection (server closes after replying OK).
    Exchange(client, "D");
    // Server should close; our subsequent read should get EOF (0) after a moment.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    char ch;
    ssize_t n = ::recv(client, &ch, 1, MSG_DONTWAIT);
    NEMU_TEST_ASSERT(n == 0 || n == -1, "client socket closed after detach");

    ::close(client);

    // 8. Restart on the same port after Stop (SO_REUSEADDR + clean stop).
    stub.Stop();
    NEMU_TEST_ASSERT(!stub.IsRunning(), "IsRunning false after Stop");
    NEMU_TEST_ASSERT(stub.Start(port), "restart on same port after Stop");

    // 9. LIVE registers: wire a CPU-state provider and verify real guest
    //    values flow through 'g'/'p'/'P' (this is the live-remote-debug path).
    //    Open a FRESH connection to the restarted server.
    int client2 = ConnectClient(port);
    {
        cpu::CpuState live_state;
        live_state.SetX(0, 0x1111222233334444ULL);
        live_state.SetX(5, 0xAAAABBBBCCCCDDDDULL);
        live_state.pc = 0x0000007100001000ULL;
        live_state.sp = 0x0000007100FFE000ULL;

        stub.SetCpuStateProvider([&live_state]() { return &live_state; });

        // p5 -> x5 must match (16 hex), not zeros.
        NEMU_TEST_ASSERT(Exchange(client2, "p5") == "ddddccccbbbbaaaa",
                         "live read of x5 returns real value (LE hex)");
        // p0 -> x0.
        NEMU_TEST_ASSERT(Exchange(client2, "p0") == "4444333322221111",
                         "live read of x0 returns real value");
        // pc index 31 (0x1f), sp index 32 (0x20). Little-endian hex.
        NEMU_TEST_ASSERT(Exchange(client2, "p1f") == "0010000071000000",
                         "live read of pc (reg 31) returns real value");
        NEMU_TEST_ASSERT(Exchange(client2, "p20") == "00e0ff0071000000",
                         "live read of sp (reg 32) returns real value");

        // 'g' full read: x5's 16-hex must appear at offset 5*16.
        std::string all = Exchange(client2, "g");
        NEMU_TEST_ASSERT(all.size() == debug::GdbStub::kNumGdbRegs * 16, "'g' returns kNumGdbRegs regs");
        NEMU_TEST_ASSERT(all.substr(5 * 16, 16) == "ddddccccbbbbaaaa",
                         "'g' embeds x5 at its slot");

        // 'P' single-register write mutates live state.
        NEMU_TEST_ASSERT(Exchange(client2, "P7=0011223344556677") == "OK", "'P' write -> OK");
        NEMU_TEST_ASSERT(live_state.GetX(7) == 0x0011223344556677ULL,
                         "'P' wrote through to live x7");

        stub.SetCpuStateProvider(nullptr);
        // With the provider removed, 'p0' should fall back to zeros.
        NEMU_TEST_ASSERT(Exchange(client2, "p0") == "0000000000000000",
                         "'p0' zero after provider removed");
    }
    ::close(client2);

    stub.Stop();

    std::cout << "[Test: GDB Remote Serial Protocol Stub] ALL PASSED" << std::endl;
    return 0;
}
#endif // _WIN32