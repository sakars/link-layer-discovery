
#include "lldp_monitor.hh"

#include <cstring>

namespace ndisc
{
    void lldpFrameParser(NeighbourList &neighbour_list, const sockaddr_ll &address, std::span<const std::byte> frame)
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

        if (address.sll_halen != ETH_ALEN)
        {
            std::cerr << "LLDP packet address wrong size" << address.sll_halen << "\n";
            return;
        }

        std::array<char, IF_NAMESIZE> interface_name{};
        if_indextoname(address.sll_ifindex, interface_name.data());
        std::optional<LLDPDataUnit> data_unit = LLDPDataUnit::FromSpan(frame);
        if (!data_unit.has_value())
        {
            std::cerr << "Failed to parse a data_unit\n";
            return;
        }
        std::optional<std::array<std::byte, 4>> ip_address = std::nullopt;
        for (const LLDPDUTypeLengthValue &tlv : data_unit->optional_tlv)
        {
            if (tlv.type == lldp::MANAGEMENT_ADDRESS && tlv.value.size() == sizeof(in_addr))
            {
                ip_address = std::array<std::byte, 4>();
                std::copy(tlv.value.begin(), tlv.value.end(), ip_address->begin());
            }
        }
        std::array<std::byte, 2> time_to_live_data{data_unit->time_to_live.value[0], data_unit->time_to_live.value[1]};

        NeighbourEntry entry{
            .chassis_id = data_unit->chassis_id.value,
            .port_id = data_unit->port_id.value,
            .time_to_live = ntohs(std::bit_cast<uint16_t>(time_to_live_data)),
            .ip_address = ip_address,
        };
        std::string chassis;
        chassis.resize(entry.chassis_id.size());
        // std::copy(entry.chassis_id.begin(), entry.chassis_id.end(), chassis.begin());
        std::memcpy(chassis.data(), entry.chassis_id.data(), entry.chassis_id.size());
        neighbour_list.chassis_map[entry.chassis_id][entry.port_id] = std::move(entry);
    }

    std::unique_ptr<EthernetLldpMonitor> EthernetLldpMonitor::Create(Callback callback)
    {
        OwnedFileDescriptor socket_fd{socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_LLDP))};
        if (!socket_fd.IsValid())
        {
            std::cerr << "Monitor errno: " << errno << "\n";
            return nullptr;
        }
        return std::make_unique<EthernetLldpMonitor>(EthernetLldpMonitor(std::move(socket_fd), std::move(callback)));
    }

    int EthernetLldpMonitor::GetSocket() const
    {
        return *socket_fd_;
    }

    void EthernetLldpMonitor::Call()
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

        ssize_t peek_packet_length = recvmsg(*socket_fd_, &message_header, MSG_PEEK | MSG_TRUNC);

        if (peek_packet_length > static_cast<ssize_t>(message_buffer_.size()))
        {
            message_buffer_.resize(peek_packet_length > MAX_FRAME_BUFFER_SIZE ? MAX_FRAME_BUFFER_SIZE : peek_packet_length);
        }

        message_iovec.iov_base = message_buffer_.data();
        message_iovec.iov_len = message_buffer_.size();

        ssize_t received_length = recvmsg(*socket_fd_, &message_header, 0);

        if (received_length == peek_packet_length)
        {
            callback_(link_layer_address, std::span<const std::byte>(message_buffer_.begin(), received_length));
        }
        else
        {
            std::cerr << "Failed to read LLDP frame\n";
        }
    }

    uint32_t EthernetLldpMonitor::GetEvents() const
    {
        return EPOLLIN;
    }
} // namespace ndisc