#ifndef LLDP_MONITOR_HH
#define LLDP_MONITOR_HH

#include <arpa/inet.h>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <linux/if_packet.h>
#include <map>
#include <memory>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <optional>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "event_handlers.hh"
#include "lldp.hh"
#include "lldp_packet.hh"

namespace ndisc
{
    constexpr uint16_t MAX_FRAME_BUFFER_SIZE = 1600;

    class EthernetLldpMonitor final : public EventHandler
    {
    public:
        using Callback = std::function<void(const sockaddr_ll &, std::span<const uint8_t>)>;

    private:
        int socket_fd_;
        std::vector<uint8_t> message_buffer_;
        Callback callback_;

        EthernetLldpMonitor(int socket_fd, Callback callback) : socket_fd_(socket_fd), callback_(std::move(callback))
        {
            message_buffer_.resize(2);
        }

    public:
        static std::unique_ptr<EthernetLldpMonitor> Create(Callback callback);

        int GetSocket() const override;

        void Call() override;

        uint32_t GetEvents() const override;
    };

    struct NeighbourEntry
    {
        std::vector<uint8_t> chassis_id;
        std::vector<uint8_t> port_id;
        uint16_t time_to_live = 0;
        std::optional<std::array<uint8_t, sizeof(in_addr)>> ip_address;
    };

    struct NeighbourList
    {
        std::map<std::vector<uint8_t>, std::map<std::vector<uint8_t>, NeighbourEntry>> chassis_map;
    };

    void lldpFrameParser(NeighbourList &neighbour_list, const sockaddr_ll &address, std::span<const uint8_t> frame);

} // namespace ndisc

#endif
