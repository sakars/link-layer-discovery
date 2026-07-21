#include "lldp_packet.hh"

#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <span>
#include <stdint.h>

namespace ndisc
{

    std::vector<std::byte> LLDPDUTypeLengthValue::ToFrameBuffer() const
    {
        const uint16_t length = value.size();
        uint16_t header = htons((type << lldp::TYPE_BIT_OFFSET) | length);
        std::span<std::byte> header_view = std::span<std::byte>(reinterpret_cast<std::byte *>(&header), sizeof(header));
        std::vector<std::byte> buffer{};
        buffer.reserve(sizeof(header) + length);
        buffer.insert(std::end(buffer), std::begin(header_view), std::end(header_view));
        buffer.insert(std::end(buffer), std::begin(value), std::end(value));
        return buffer;
    }

    std::optional<LLDPDUTypeLengthValue> LLDPDUTypeLengthValue::FromSpan(std::span<const std::byte> &tlv_bytes)
    {
        if (tlv_bytes.size() < sizeof(uint16_t))
        {
            return std::nullopt;
        }
        const uint16_t tlv_header_raw = (std::to_integer<uint16_t>(tlv_bytes[1]) << 8) | std::to_integer<uint16_t>(tlv_bytes[0]);
        const uint16_t tlv_header = ntohs(tlv_header_raw);
        const ssize_t length = tlv_header & lldp::TYPE_MASK;
        const lldp::TLV type = (lldp::TLV)(tlv_header >> lldp::TYPE_BIT_OFFSET);
        if (length + sizeof(tlv_header) > tlv_bytes.size())
        {
            return std::nullopt;
        }
        LLDPDUTypeLengthValue tlv_structure{};

        tlv_structure.type = type;
        tlv_structure.value = std::vector<std::byte>(tlv_bytes.begin() + sizeof(tlv_header), tlv_bytes.begin() + sizeof(tlv_header) + length);

        tlv_bytes = tlv_bytes.subspan(length + sizeof(tlv_header));

        return tlv_structure;
    }

    constexpr std::array<std::byte, 2> END_OF_DATA_UNIT_TLV{std::byte{0x00}, std::byte{0x00}};

    std::vector<std::byte> LLDPDataUnit::ToFrameBuffer() const
    {
        std::vector<std::byte> buffer{};
        const std::vector<std::byte> chassis_id_buffer = chassis_id.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(chassis_id_buffer), std::end(chassis_id_buffer));
        const std::vector<std::byte> port_id_buffer = port_id.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(port_id_buffer), std::end(port_id_buffer));
        const std::vector<std::byte> time_to_live_buffer = time_to_live.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(time_to_live_buffer), std::end(time_to_live_buffer));
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            const std::vector<std::byte> tlv_buffer = tlv.ToFrameBuffer();
            buffer.insert(std::end(buffer), std::begin(tlv_buffer), std::end(tlv_buffer));
        }
        buffer.insert(std::end(buffer), std::begin(END_OF_DATA_UNIT_TLV), std::end(END_OF_DATA_UNIT_TLV));
        return buffer;
    }

    std::optional<LLDPDataUnit> LLDPDataUnit::FromSpan(std::span<const std::byte> data_unit_bytes)
    {
        const std::optional<LLDPDUTypeLengthValue> chassis_id_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!chassis_id_tlv.has_value())
        {
            std::cerr << "Failed to parse chassis tlv\n";
            return std::nullopt;
        }
        if (chassis_id_tlv->type != lldp::CHASSIS_ID)
        {
            std::cerr << "Chassis tlv has incorrect type\n";
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> port_id_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!port_id_tlv.has_value())
        {
            std::cerr << "Failed to read Port tlv\n";
            return std::nullopt;
        }
        if (port_id_tlv->type != lldp::PORT_ID)
        {
            std::cerr << "Port tlv type incorrect\n";
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> ttl_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!ttl_tlv.has_value())
        {
            std::cerr << "Failed to read TTL\n";
            return std::nullopt;
        }
        if (ttl_tlv->type != lldp::TIME_TO_LIVE)
        {
            std::cerr << "TTL tlv incorrect type\n";
            return std::nullopt;
        }
        std::vector<LLDPDUTypeLengthValue> other_tlvs;
        while (std::optional<LLDPDUTypeLengthValue> tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes))
        {
            if (tlv->type == lldp::END_OF_LLDPDU)
            {
                break;
            }
            other_tlvs.emplace_back(std::move(*tlv));
        }
        return LLDPDataUnit{
            .chassis_id = *chassis_id_tlv,
            .port_id = *port_id_tlv,
            .time_to_live = *ttl_tlv,
            .optional_tlv = std::move(other_tlvs),
        };
    }

    std::vector<std::byte> LLDPEthernetFrame::ToFrameBuffer() const
    {
        const std::vector<std::byte> data_unit_buffer = data_unit.ToFrameBuffer();
        const auto ether_type = std::bit_cast<std::array<std::byte, lldp::ETHERTYPE_SIZE>>(header.ether_type);

        std::vector<std::byte> buffer{};
        buffer.resize(sizeof(header.ether_dhost) + sizeof(header.ether_shost) + sizeof(header.ether_type) + data_unit_buffer.size());
        std::byte *iter = buffer.data();
        std::memcpy(iter, std::begin(header.ether_dhost), std::size(header.ether_dhost));
        std::advance(iter, std::size(header.ether_dhost));
        std::memcpy(iter, std::begin(header.ether_shost), std::size(header.ether_shost));
        std::advance(iter, std::size(header.ether_shost));
        std::memcpy(iter, std::begin(ether_type), std::size(ether_type));
        std::advance(iter, std::size(ether_type));
        std::memcpy(iter, data_unit_buffer.data(), data_unit_buffer.size());
        return buffer;
    }

} // namespace ndisc
