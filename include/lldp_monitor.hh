#ifndef LLDP_MONITOR_HH
#define LLDP_MONITOR_HH

#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <optional>
#include <memory>
#include <functional>
#include <span>
#include "event_handlers.hh"

namespace ndisc
{
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
        static std::unique_ptr<EthernetLldpMonitor> Create(int device_idx, Callback callback)
        {
            int socket_fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_LLDP));
            if (socket_fd < 0)
            {
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

            if (peek_packet_length > message_buffer_.size())
            {
                message_buffer_.resize(peek_packet_length);
            }

            message_iovec.iov_base = message_buffer_.data();
            message_iovec.iov_len = message_buffer_.size();

            ssize_t received_length = recvmsg(socket_fd_, &message_header, 0);

            callback_(link_layer_address, std::span<const uint8_t>(message_buffer_.begin(), received_length));
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
    };
    // class LldpMonitor
    // {
    //     int socket_fd_ = -1;
    //     int interface_idx_;

    //     LldpMonitor(int socket_fd, int if_idx) : socket_fd_(socket_fd), interface_idx_(if_idx) {}

    // public:
    //     void Call() override
    //     {
    //         // TODO: Read LLDP packets
    //         std::cout << "LLDP Packet received\n";
    //     }

    //     void Register(int epfd) override
    //     {
    //         epoll_event events{};
    //         events.events = EPOLLIN;
    //         events.data.ptr = this;

    //         if (epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd_, &events) != 0)
    //         {
    //             std::cerr << "Failed to add socket to epoll\n";
    //         }
    //     }

    //     void Deregister(int epfd) override
    //     {
    //         epoll_event events{};
    //         if (epoll_ctl(epfd, EPOLL_CTL_DEL, socket_fd_, &events) != 0)
    //         {
    //             std::cerr << "Failed to delete epoll register\n";
    //         }
    //     }

    //     static std::unique_ptr<LldpMonitor> Create(int interface_idx)
    //     {
    //         uint16_t protocol = htons(ETH_P_LLDP);
    //         int socket_fd = socket(AF_PACKET, SOCK_RAW, protocol);
    //         if (socket_fd < 0)
    //         {
    //             std::cerr << "Failed to create socket...\n";
    //             return nullptr;
    //         }
    //         sockaddr_ll address{
    //             .sll_family = AF_PACKET,
    //             .sll_protocol = protocol,
    //             .sll_ifindex = interface_idx,
    //         };
    //         int bind_result = bind(socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    //         if (bind_result != 0)
    //         {
    //             std::cerr << "Bind failed...\n";
    //             close(socket_fd);
    //             return nullptr;
    //         }
    //         LldpMonitor monitor = LldpMonitor(socket_fd, interface_idx);
    //         std::unique_ptr<LldpMonitor> pinned_monitor = std::make_unique<LldpMonitor>(std::move(monitor));

    //         return pinned_monitor;
    //     }
    // };
} // namespace ndisc

#endif
