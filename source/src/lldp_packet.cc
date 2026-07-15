
#include "lldp_packet.hh"
#include <arpa/inet.h>
#include <array>
#include <bit>
#include <iostream>
#include <span>
#include <stdint.h>

namespace ndisc
{

    std::vector<uint8_t> LLDPDUTypeLengthValue::ToFrameBuffer() const
    {
        const uint16_t length = value.size();
        uint16_t header = htons((type << lldp::TYPE_BIT_OFFSET) | length);
        std::span<uint8_t> header_view = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&header), sizeof(header));
        std::vector<uint8_t> buffer{};
        buffer.reserve(sizeof(header) + length);
        buffer.insert(std::end(buffer), std::begin(header_view), std::end(header_view));
        buffer.insert(std::end(buffer), std::begin(value), std::end(value));
        return buffer;
    }

    std::optional<LLDPDUTypeLengthValue> LLDPDUTypeLengthValue::FromSpan(std::span<const uint8_t> &tlv_bytes)
    {
        if (tlv_bytes.size() < sizeof(uint16_t))
        {
            return std::nullopt;
        }
        const uint16_t tlv_header_raw = (tlv_bytes[1] << 8) | tlv_bytes[0];
        const uint16_t tlv_header = ntohs(tlv_header_raw);
        const ssize_t length = tlv_header & lldp::TYPE_MASK;
        const lldp::TLV type = (lldp::TLV)(tlv_header >> lldp::TYPE_BIT_OFFSET);
        if (length + sizeof(tlv_header) > tlv_bytes.size())
        {
            return std::nullopt;
        }
        LLDPDUTypeLengthValue tlv_structure{};

        tlv_structure.type = type;
        tlv_structure.value = std::vector<uint8_t>(tlv_bytes.begin() + sizeof(tlv_header), tlv_bytes.begin() + sizeof(tlv_header) + length);

        tlv_bytes = tlv_bytes.subspan(length + sizeof(tlv_header));

        return tlv_structure;
    }

    std::vector<uint8_t> LLDPDataUnit::ToFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        const std::vector<uint8_t> chassis_id_buffer = chassis_id.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(chassis_id_buffer), std::end(chassis_id_buffer));
        const std::vector<uint8_t> port_id_buffer = port_id.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(port_id_buffer), std::end(port_id_buffer));
        const std::vector<uint8_t> time_to_live_buffer = time_to_live.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(time_to_live_buffer), std::end(time_to_live_buffer));
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            const std::vector<uint8_t> tlv_buffer = tlv.ToFrameBuffer();
            buffer.insert(std::end(buffer), std::begin(tlv_buffer), std::end(tlv_buffer));
        }
        constexpr std::array<uint8_t, 2> end_of_data_unit{{0, 0}};
        buffer.insert(std::end(buffer), std::begin(end_of_data_unit), std::end(end_of_data_unit));
        return buffer;
    }

    std::optional<LLDPDataUnit> LLDPDataUnit::FromSpan(std::span<const uint8_t> data_unit_bytes)
    {
        const std::optional<LLDPDUTypeLengthValue> chassis_id_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!chassis_id_tlv.has_value())
        {
            return std::nullopt;
        }
        if (chassis_id_tlv->type != lldp::CHASSIS_ID)
        {
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> port_id_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!port_id_tlv.has_value())
        {
            return std::nullopt;
        }
        if (port_id_tlv->type != lldp::PORT_ID)
        {
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> ttl_tlv = LLDPDUTypeLengthValue::FromSpan(data_unit_bytes);
        if (!ttl_tlv.has_value())
        {
            return std::nullopt;
        }
        if (ttl_tlv->type != lldp::TIME_TO_LIVE)
        {
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

    std::vector<uint8_t> LLDPEthernetFrame::ToFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        buffer.insert(std::end(buffer), std::begin(header.ether_dhost), std::end(header.ether_dhost));
        buffer.insert(std::end(buffer), std::begin(header.ether_shost), std::end(header.ether_shost));
        const auto ether_type = std::bit_cast<std::array<uint8_t, lldp::ETHERTYPE_SIZE>>(header.ether_type);
        buffer.insert(std::end(buffer), std::begin(ether_type), std::end(ether_type));

        const std::vector<uint8_t> data_unit_buffer = data_unit.ToFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(data_unit_buffer), std::end(data_unit_buffer));
        return buffer;
    }

} // namespace ndisc
