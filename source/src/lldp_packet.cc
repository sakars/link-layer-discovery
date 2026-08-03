#include "lldp_packet.hh"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <ranges>
#include <span>
#include <stdint.h>

namespace lldp
{
    size_t LLDPDUTypeLengthValue::GetFrameBufferSize() const
    {
        return sizeof(uint16_t) + value.size();
    }

    std::span<std::byte>::iterator LLDPDUTypeLengthValue::ToFrameBuffer(std::span<std::byte>::iterator iter) const
    {
        const uint16_t length = value.size();
        const uint16_t header = htons((type << lldp::TYPE_BIT_OFFSET) | length);
        iter = std::ranges::copy(std::as_bytes(std::span{&header, 1}), iter).out;
        iter = std::ranges::copy(value, iter).out;
        return iter;
    }

    std::optional<LLDPDUTypeLengthValue> LLDPDUTypeLengthValue::FromSpan(std::span<const std::byte> &tlv_bytes)
    {
        if (tlv_bytes.size() < sizeof(uint16_t))
        {
            return std::nullopt;
        }
        uint16_t tlv_header_network_order = 0;
        std::ranges::copy(tlv_bytes.first<sizeof(uint16_t)>(), std::as_writable_bytes(std::span(&tlv_header_network_order, 1)).begin());
        const uint16_t tlv_header = ntohs(tlv_header_network_order);
        const ssize_t length = tlv_header & lldp::TYPE_MASK;
        const lldp::TLV type = (lldp::TLV)(tlv_header >> lldp::TYPE_BIT_OFFSET);
        if (length + sizeof(tlv_header) > tlv_bytes.size())
        {
            return std::nullopt;
        }
        LLDPDUTypeLengthValue tlv_structure{};

        tlv_structure.type = type;
        tlv_structure.value = tlv_bytes.subspan(sizeof(tlv_header), length);
        tlv_bytes = tlv_bytes.subspan(length + sizeof(tlv_header));

        return tlv_structure;
    }

    constexpr std::array<std::byte, 2> END_OF_DATA_UNIT_TLV{std::byte{0x00}, std::byte{0x00}};

    size_t LLDPDataUnit::GetFrameBufferSize() const
    {
        size_t size = chassis_id.GetFrameBufferSize() + port_id.GetFrameBufferSize() + time_to_live.GetFrameBufferSize();
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            size += tlv.GetFrameBufferSize();
        }
        size += sizeof(END_OF_DATA_UNIT_TLV);
        return size;
    }

    std::span<std::byte>::iterator LLDPDataUnit::ToFrameBuffer(std::span<std::byte>::iterator iter) const
    {
        iter = chassis_id.ToFrameBuffer(iter);
        iter = port_id.ToFrameBuffer(iter);
        iter = time_to_live.ToFrameBuffer(iter);
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            iter = tlv.ToFrameBuffer(iter);
        }
        iter = std::ranges::copy(END_OF_DATA_UNIT_TLV, iter).out;
        return iter;
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

    size_t LLDPEthernetFrame::GetFrameBufferSize() const
    {
        return sizeof(header) + data_unit.GetFrameBufferSize();
    }

    std::span<std::byte>::iterator LLDPEthernetFrame::ToFrameBuffer(std::span<std::byte>::iterator iter) const
    {
        iter = std::ranges::copy(std::as_bytes(std::span{&header, 1}), iter).out;
        iter = data_unit.ToFrameBuffer(iter);
        return iter;
    }

} // namespace lldp
