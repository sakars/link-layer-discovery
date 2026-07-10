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
        static std::unique_ptr<EthernetLldpMonitor> Create(Callback callback)
        {
            int socket_fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_LLDP));
            if (socket_fd < 0)
            {
                std::cerr << "Monitor errno: " << errno << "\n";
                return nullptr;
            }
            return std::make_unique<EthernetLldpMonitor>(EthernetLldpMonitor(socket_fd, std::move(callback)));
        }

        int GetSocket() const override
        {
            return socket_fd_;
        }

        void Call() override
        {
            sockaddr_ll link_layer_address{};
            iovec message_iovec{};
            message_iovec.iov_base = message_buffer_.data();
            message_iovec.iov_len = message_buffer_.size();
            msghdr message_header{};
            message_header.msg_name = &link_layer_address;
            message_header.msg_namelen = sizeof(link_layer_address);
            message_header.msg_iov = &message_iovec;
            message_header.msg_iovlen = 1;
            message_header.msg_control = nullptr;
            message_header.msg_controllen = 0;
            message_header.msg_flags = 0;

            ssize_t peek_packet_length = recvmsg(socket_fd_, &message_header, MSG_PEEK | MSG_TRUNC);

            if (peek_packet_length > static_cast<ssize_t>(message_buffer_.size()))
            {
                message_buffer_.resize(peek_packet_length > MAX_FRAME_BUFFER_SIZE ? MAX_FRAME_BUFFER_SIZE : peek_packet_length);
            }

            message_iovec.iov_base = message_buffer_.data();
            message_iovec.iov_len = message_buffer_.size();

            ssize_t received_length = recvmsg(socket_fd_, &message_header, 0);

            if (received_length == peek_packet_length)
            {
                callback_(link_layer_address, std::span<const uint8_t>(message_buffer_.begin(), received_length));
            }
            else
            {
                std::cerr << "Failed to read LLDP frame\n";
            }
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
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
