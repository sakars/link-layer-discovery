
#include "lldp_monitor.hh"

namespace ndisc
{
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
        std::optional<std::array<uint8_t, 4>> ip_address = std::nullopt;
        for (const LLDPDUTypeLengthValue &tlv : data_unit->optional_tlv)
        {
            if (tlv.type == lldp::MANAGEMENT_ADDRESS && tlv.value.size() == sizeof(in_addr))
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
        std::string chassis;
        chassis.resize(entry.chassis_id.size());
        std::copy(entry.chassis_id.begin(), entry.chassis_id.end(), chassis.begin());
        neighbour_list.chassis_map[entry.chassis_id][entry.port_id] = std::move(entry);
    }
} // namespace ndisc