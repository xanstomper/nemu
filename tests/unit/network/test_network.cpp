#include "core/network/ldn_network.hpp"
#include <iostream>
#include <vector>
#include <cstring>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::network;

int main() {
    std::cout << "========================================\n";
    std::cout << "   NEMU LDN & NINTENDO PIA NETWORK TESTS\n";
    std::cout << "========================================\n";

    // 1. Initialize Network
    LdnUdpNetwork net;
    NEMU_TEST_ASSERT(net.Initialize(), "LdnUdpNetwork initialization succeeds");
    NEMU_TEST_ASSERT(net.IsOnline(), "Network reports online state");

    auto mac = net.LocalMac();
    bool non_zero_mac = false;
    for (u8 b : mac) {
        if (b != 0) non_zero_mac = true;
    }
    NEMU_TEST_ASSERT(non_zero_mac, "Local MAC is valid non-zero hardware identity");

    // 2. Test LdnStation State Machine
    LdnStation host_station(net);
    NEMU_TEST_ASSERT(host_station.GetState() == LdnStation::State::Initialized, "Initial state is Initialized");

    // Create Access Point (Host lobby)
    const char* lobby_name = "Mario Kart 8 Deluxe Grand Prix";
    const u32 game_id = 0x0100152000022000 & 0xFFFFFFFF;
    NEMU_TEST_ASSERT(host_station.CreateAccessPoint(lobby_name, game_id, 4), "CreateAccessPoint succeeds");
    NEMU_TEST_ASSERT(host_station.GetState() == LdnStation::State::AccessPointOpened, "State is AccessPointOpened");
    NEMU_TEST_ASSERT(host_station.PlayerCount() == 1, "Host starts with 1 player");
    NEMU_TEST_ASSERT(host_station.GetNodeId() == 0, "Host is assigned Node ID 0");

    // 3. Test Broadcast and Self Identification
    const auto& session = host_station.LocalSession();
    NEMU_TEST_ASSERT(std::strcmp(session.name, lobby_name) == 0, "Session name matches lobby name");
    NEMU_TEST_ASSERT(session.max_players == 4, "Max players is 4");
    NEMU_TEST_ASSERT(net.Broadcast(session), "Broadcast session announcement succeeds");

    // 4. Test Scan
    auto scan_res = host_station.Scan(game_id);
    std::cout << "  - Station scan returned " << scan_res.size() << " lobbies\n";

    // 5. Test Nintendo PIA Packet Dispatch (Small Packet)
    const std::vector<u8> pia_small_payload = {0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
    NEMU_TEST_ASSERT(net.SendPiaPacket(session.host_mac, 1, pia_small_payload.data(), pia_small_payload.size()),
                     "Send small unfragmented PIA packet succeeds");

    // 6. Test Nintendo PIA Multi-Fragment Packet Dispatch (Large Packet > MTU)
    std::vector<u8> pia_large_payload(3500); // 3500 bytes -> 3 fragments
    for (size_t i = 0; i < pia_large_payload.size(); ++i) {
        pia_large_payload[i] = static_cast<u8>((i * 7 + 13) & 0xFF);
    }
    NEMU_TEST_ASSERT(net.SendPiaPacket(session.host_mac, 2, pia_large_payload.data(), pia_large_payload.size()),
                     "Send large multi-fragment PIA packet succeeds");

    // 7. Test Disconnect
    NEMU_TEST_ASSERT(host_station.CloseAccessPoint(), "CloseAccessPoint succeeds");
    NEMU_TEST_ASSERT(host_station.GetState() == LdnStation::State::Initialized, "State resets to Initialized after disconnect");

    std::cout << "ALL LDN & NINTENDO PIA NETWORK TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
