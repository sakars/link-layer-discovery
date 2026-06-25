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
#include "event_handlers.hh"

namespace ndisc
{
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
