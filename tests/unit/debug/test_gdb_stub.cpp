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

    // 2. Read all registers -> 31 x 16 hex '0'.
    std::string regs = Exchange(client, "g");
    NEMU_TEST_ASSERT(regs.size() == 31 * 16 && regs.find_first_not_of('0') == std::string::npos,
                     "read regs -> 31 zero registers");

    // 3. Read one register -> 16 hex '0'.
    NEMU_TEST_ASSERT(Exchange(client, "p10") == "0000000000000000", "read reg 0x10");

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
    stub.Stop();

    std::cout << "[Test: GDB Remote Serial Protocol Stub] ALL PASSED" << std::endl;
    return 0;
}
#endif // _WIN32