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
#include <net/if.h>
#include <net/if_arp.h>
#include <iomanip>
#include <chrono>
#include <map>

#include "event_handlers.hh"
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
        uint16_t time_to_live;
        std::optional<std::array<uint8_t, 4>> ip_address;
    };

    struct NeighbourList
    {
        std::map<std::vector<uint8_t>, std::map<std::vector<uint8_t>, NeighbourEntry>> chassis_map;
    };

    void lldpFrameParser(NeighbourList &neighbour_list, const sockaddr_ll &address, std::span<const uint8_t> frame)
    {
        if (address.sll_family != AF_PACKET)
        {
            std::cerr << "Received LLDP frame is not of AF_PACKET family\n";
            return;
        }

        if (ntohs(address.sll_protocol) != ETH_P_LLDP)
        {
            std::cerr << "Received a frame that is not LLDP\n";
            return;
        }

        if (address.sll_hatype != ARPHRD_ETHER)
        {
            std::cerr << "LLDP packet does not originate from ethernet\n";
            return;
        }

        if (address.sll_halen != 8)
        {
            std::cerr << "LLDP packet address wrong size" << address.sll_halen << "\n";
            return;
        }

        std::ios_base::fmtflags f(std::cout.flags());
        std::cout << std::hex << std::setw(2) << std::setfill('0');
        std::cout << address.sll_addr[0];
        for (const unsigned char &data : std::span<const unsigned char>(address.sll_addr).subspan(1))
        {
            std::cout << ":" << data;
        }
        std::cout.flags(f);

        std::array<char, IF_NAMESIZE> interface_name{};
        if_indextoname(address.sll_ifindex, interface_name.data());
        std::cout << "LLDP packet incoming from " << address.sll_ifindex << " interface: " << interface_name.data() << "\n";
        std::optional<LLDPDataUnit> data_unit = LLDPDataUnit::fromSpan(frame);
        if (!data_unit.has_value())
        {
            std::cerr << "Failed to parse a data_unit\n";
            return;
        }
        std::optional<std::array<uint8_t, 4>> ip_address = std::nullopt;
        for (const LLDPDUTypeLengthValue &tlv : data_unit->optional_tlv)
        {
            if (tlv.type == 8 && tlv.value.size() == 4)
            {
                ip_address = std::array<uint8_t, 4>();
                std::copy(tlv.value.begin(), tlv.value.end(), ip_address->begin());
            }
        }
        std::array<uint8_t, 2> time_to_live_data{data_unit->time_to_live.value[0], data_unit->time_to_live.value[1]};

        NeighbourEntry entry{
            .chassis_id = data_unit->chassis_id.value,
            .port_id = data_unit->port_id.value,
            .time_to_live = ntohs(std::bit_cast<uint16_t>(time_to_live_data)),
            .ip_address = ip_address,
        };
        neighbour_list.chassis_map[entry.chassis_id][entry.port_id] = std::move(entry);
    }

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
