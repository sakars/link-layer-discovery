#ifndef LLDP_MONITOR_HH
#define LLDP_MONITOR_HH

#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
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
        using Callback = std::function<void(const sockaddr_ll &, std::span<const std::byte>)>;

    private:
        OwnedFileDescriptor socket_fd_;
        std::vector<std::byte> message_buffer_;
        Callback callback_;

        EthernetLldpMonitor(OwnedFileDescriptor &&socket_fd, Callback callback) : socket_fd_(std::move(socket_fd)), callback_(std::move(callback))
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
        std::vector<std::byte> chassis_id;
        std::vector<std::byte> port_id;
        uint16_t time_to_live = 0;
        std::optional<std::array<std::byte, sizeof(in_addr)>> ip_address;
    };

    struct NeighbourList
    {
        std::map<std::vector<std::byte>, std::map<std::vector<std::byte>, NeighbourEntry>> chassis_map;

        void Tick(const uint64_t &);
    };

    void lldpFrameParser(NeighbourList &neighbour_list, const sockaddr_ll &address, std::span<const std::byte> frame);

} // namespace ndisc

#endif
