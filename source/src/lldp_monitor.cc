
#include "lldp_monitor.hh"

#include <cstring>
#include <variant>

namespace ndisc
{
    static inline std::optional<std::variant<
        std::span<const std::byte, sizeof(in_addr)>,
        std::span<const std::byte, sizeof(in6_addr)>,
        std::span<const std::byte, ETH_ALEN>>>
    managementTlvGetManagementString(const std::vector<std::byte> &management_tlv_value)
    {
        if (management_tlv_value.empty())
        {
            return std::nullopt;
        }
        uint8_t length = std::to_integer<uint8_t>(management_tlv_value.at(0));
        if (length == 0 || management_tlv_value.size() < 1U + length)
        {
            return std::nullopt;
        }
        std::byte string_subtype = management_tlv_value.at(1);
        if (string_subtype == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4)
        {
            if (length != 1 + sizeof(in_addr))
            {
                return std::nullopt;
            }
            return std::span<const std::byte, sizeof(in_addr)>(std::next(management_tlv_value.begin(), 2), sizeof(in_addr));
        }
        if (string_subtype == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6)
        {
            if (length != 1 + sizeof(in6_addr))
            {
                return std::nullopt;
            }
            return std::span<const std::byte, sizeof(in6_addr)>(std::next(management_tlv_value.data(), 2), sizeof(in6_addr));
        }
        if (string_subtype == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC)
        {

            if (length != 1 + ETH_ALEN)
            {
                return std::nullopt;
            }
            return std::span<const std::byte, ETH_ALEN>(std::next(management_tlv_value.begin(), 2), ETH_ALEN);
        }
        return std::nullopt;
    }

    NeighbourEntry lldpduToNeighbourEntry(const lldp::LLDPDataUnit &lldpdu)
    {
        std::array<std::byte, 2> time_to_live_data{lldpdu.time_to_live.value[0], lldpdu.time_to_live.value[1]};

        std::optional<std::array<std::byte, sizeof(in_addr)>> ipv4_address = std::nullopt;
        std::optional<std::array<std::byte, sizeof(in6_addr)>> ipv6_address = std::nullopt;
        for (const lldp::LLDPDUTypeLengthValue &tlv : lldpdu.optional_tlv)
        {
            if (tlv.type == lldp::MANAGEMENT_ADDRESS)
            {
                auto address_string = managementTlvGetManagementString(tlv.value);
                if (address_string.has_value())
                {
                    if (std::span<const std::byte, sizeof(in_addr)> *addr = std::get_if<std::span<const std::byte, sizeof(in_addr)>>(&*address_string))
                    {
                        ipv4_address = std::array<std::byte, sizeof(in_addr)>{};
                        std::copy(addr->begin(), addr->end(), ipv4_address->begin());
                    }
                    else if (std::span<const std::byte, sizeof(in6_addr)> *addr = std::get_if<std::span<const std::byte, sizeof(in6_addr)>>(&*address_string))
                    {
                        ipv6_address = std::array<std::byte, sizeof(in6_addr)>{};
                        std::copy(addr->begin(), addr->end(), ipv6_address->begin());
                    }
                }
            }
        }

        return NeighbourEntry{
            .chassis_id = lldpdu.chassis_id.value,
            .port_id = lldpdu.port_id.value,
            .time_to_live = ntohs(std::bit_cast<uint16_t>(time_to_live_data)),
            .ipv4_address = ipv4_address,
            .ipv6_address = ipv6_address,
        };
    }

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
        const std::optional<lldp::LLDPDataUnit> data_unit = lldp::LLDPDataUnit::FromSpan(frame);
        if (!data_unit.has_value())
        {
            std::cerr << "Failed to parse a data_unit\n";
            return;
        }
        NeighbourEntry entry = lldpduToNeighbourEntry(*data_unit);
        std::string chassis;
        chassis.resize(entry.chassis_id.size());
        std::memcpy(chassis.data(), entry.chassis_id.data(), entry.chassis_id.size());
        neighbour_list.chassis_map[entry.chassis_id][entry.port_id] = std::move(entry);
    }

    std::expected<std::unique_ptr<EthernetLldpMonitor>, int> EthernetLldpMonitor::Create(Callback callback)
    {
        OwnedFileDescriptor socket_fd{socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_LLDP))};
        if (!socket_fd.IsValid())
        {
            return std::unexpected(errno);
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

    void NeighbourList::Tick(const uint64_t &delta_seconds)
    {
        std::vector<std::tuple<std::vector<std::byte>, std::vector<std::byte>>> timed_out_entries{};
        for (auto &[chassis, port_map] : chassis_map)
        {
            for (auto &[port, entry] : port_map)
            {
                if (entry.time_to_live > delta_seconds)
                {
                    entry.time_to_live -= delta_seconds;
                }
                else
                {
                    entry.time_to_live = 0;
                    timed_out_entries.emplace_back(chassis, port);
                }
            }
        }
        for (const auto &[chassis, port] : timed_out_entries)
        {
            chassis_map[chassis].erase(port);
            if (chassis_map[chassis].empty())
            {
                chassis_map.erase(chassis);
            }
        }
    }
} // namespace ndisc