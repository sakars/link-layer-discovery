
#include "lldp_packet.hh"
#include "system_information.hh"
#include <stdint.h>
#include <arpa/inet.h>
#include <array>
#include <span>
#include <bit>

namespace ndisc
{

    std::vector<uint8_t> LLDPDUTypeLengthValue::toFrameBuffer() const
    {
        const size_t TYPE_BIT_OFFSET = 9;
        const size_t LENGTH = value.size();
        uint16_t header = htons((type << TYPE_BIT_OFFSET) & LENGTH);
        std::vector<uint8_t> buffer{};
        buffer.reserve(2 + LENGTH);
        buffer.insert(std::end(buffer), reinterpret_cast<uint8_t *>(&header), reinterpret_cast<uint8_t *>(&header) + sizeof(header));
        buffer.insert(std::end(buffer), std::begin(value), std::end(value));
        return buffer;
    }

    std::optional<LLDPDUTypeLengthValue> LLDPDUTypeLengthValue::fromSpan(std::span<const uint8_t> &tlv_bytes)
    {
        if (tlv_bytes.size() < 2)
        {
            return std::nullopt;
        }
        const size_t TYPE_BIT_OFFSET = 9;
        const uint16_t type_mask = (1 << TYPE_BIT_OFFSET) - 1;
        const uint16_t tlv_header = ntohs(*tlv_bytes.data());
        const size_t length = tlv_header & type_mask;
        const uint8_t type = tlv_header >> TYPE_BIT_OFFSET;
        if (length + 2 > tlv_bytes.size())
        {
            return std::nullopt;
        }
        LLDPDUTypeLengthValue tlv_structure{};

        tlv_structure.type = type;
        tlv_structure.value = std::vector<uint8_t>(tlv_bytes.begin() + 2, tlv_bytes.begin() + 2 + length);

        tlv_bytes = tlv_bytes.subspan(length + 2);

        return tlv_structure;
    }

    std::vector<uint8_t> LLDPDataUnit::toFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        const std::vector<uint8_t> chassis_id_buffer = chassis_id.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(chassis_id_buffer), std::end(chassis_id_buffer));
        const std::vector<uint8_t> port_id_buffer = port_id.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(port_id_buffer), std::end(port_id_buffer));
        const std::vector<uint8_t> time_to_live_buffer = time_to_live.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(time_to_live_buffer), std::end(time_to_live_buffer));
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            const std::vector<uint8_t> tlv_buffer = tlv.toFrameBuffer();
            buffer.insert(std::end(buffer), std::begin(tlv_buffer), std::end(tlv_buffer));
        }
        constexpr std::array<uint8_t, 2> end_of_data_unit{{0, 0}};
        buffer.insert(std::end(buffer), std::begin(end_of_data_unit), std::end(end_of_data_unit));
        return buffer;
    }

    std::optional<LLDPDataUnit> LLDPDataUnit::fromSpan(std::span<const uint8_t> data_unit_bytes)
    {
        // TODO: literals need ID const vars
        const std::optional<LLDPDUTypeLengthValue> chassis_id_tlv = LLDPDUTypeLengthValue::fromSpan(data_unit_bytes);
        if (!chassis_id_tlv.has_value())
        {
            return std::nullopt;
        }
        if (chassis_id_tlv->type != 1)
        {
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> port_id_tlv = LLDPDUTypeLengthValue::fromSpan(data_unit_bytes);
        if (!port_id_tlv.has_value())
        {
            return std::nullopt;
        }
        if (port_id_tlv->type != 2)
        {
            return std::nullopt;
        }
        const std::optional<LLDPDUTypeLengthValue> ttl_tlv = LLDPDUTypeLengthValue::fromSpan(data_unit_bytes);
        if (!ttl_tlv.has_value())
        {
            return std::nullopt;
        }
        if (ttl_tlv->type != 3)
        {
            return std::nullopt;
        }
        std::vector<LLDPDUTypeLengthValue> other_tlvs;
        while (std::optional<LLDPDUTypeLengthValue> tlv = LLDPDUTypeLengthValue::fromSpan(data_unit_bytes))
        {
            if (tlv->type == 0)
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

    std::vector<uint8_t> LLDPEthernetFrame::toFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        buffer.insert(std::end(buffer), std::begin(header.ether_dhost), std::end(header.ether_dhost));
        buffer.insert(std::end(buffer), std::begin(header.ether_shost), std::end(header.ether_shost));
        const auto ether_type = std::bit_cast<std::array<uint8_t, 2>>(htons(header.ether_type));
        buffer.insert(std::end(buffer), std::begin(ether_type), std::end(ether_type));

        const std::vector<uint8_t> data_unit_buffer = data_unit.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(data_unit_buffer), std::end(data_unit_buffer));
        return buffer;
    }

    std::optional<LLDPEthernetFrame> LLDPEthernetFrame::fromSpan(std::span<const uint8_t> frame)
    {
        LLDPEthernetFrame generated_frame{};
        if (frame.size() < sizeof(generated_frame.header))
        {
            return std::nullopt;
        }
        std::span<uint8_t> frame_contents = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&(generated_frame.header)), sizeof(generated_frame.header));
        std::copy(frame.begin(), frame.begin() + sizeof(ether_header), frame_contents.begin());
        // TODO: Continue
        return std::nullopt;
    }

    LLDPDUTypeLengthValue createChassisIdTLV()
    {
        constexpr uint8_t K_CHASSIS_TLV_TYPE = 1;
        constexpr uint8_t K_CHASSIS_TLV_LOCALLY_ASSIGNED = 7;
        const std::string MACHINE_ID = GetMachineId();

        LLDPDUTypeLengthValue chassis_tlv;
        chassis_tlv.type = K_CHASSIS_TLV_TYPE;
        chassis_tlv.value.push_back(K_CHASSIS_TLV_LOCALLY_ASSIGNED);
        chassis_tlv.value.insert(std::end(chassis_tlv.value), std::begin(MACHINE_ID), std::end(MACHINE_ID));
        return chassis_tlv;
    }

    // LLDPDUTypeLengthValue createPortIdTLV(const std::string &interface)
    // {
    // }
} // namespace ndisc
