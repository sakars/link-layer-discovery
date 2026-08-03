#ifndef LLDP_PACKET_HH
#define LLDP_PACKET_HH

#include <net/ethernet.h>
#include <optional>
#include <span>
#include <stdint.h>
#include <vector>

#include "lldp.hh"

namespace lldp
{
    struct LLDPDUTypeLengthValue
    {
        lldp::TLV type;
        std::span<const std::byte> value;

        size_t GetFrameBufferSize() const;
        std::span<std::byte>::iterator ToFrameBuffer(std::span<std::byte>::iterator) const;

        static std::optional<LLDPDUTypeLengthValue> FromSpan(std::span<const std::byte> &);
    };

    struct LLDPDataUnit
    {
        LLDPDUTypeLengthValue chassis_id;
        LLDPDUTypeLengthValue port_id;
        LLDPDUTypeLengthValue time_to_live;
        std::vector<LLDPDUTypeLengthValue> optional_tlv;

        size_t GetFrameBufferSize() const;
        std::span<std::byte>::iterator ToFrameBuffer(std::span<std::byte>::iterator) const;
        static std::optional<LLDPDataUnit> FromSpan(std::span<const std::byte>);
    };

    struct LLDPEthernetFrame
    {
        ether_header header{};
        LLDPDataUnit data_unit;

        size_t GetFrameBufferSize() const;
        std::span<std::byte>::iterator ToFrameBuffer(std::span<std::byte>::iterator) const;
    };

    // LLDPDUTypeLengthValue createChassisIdTLV();
} // namespace lldp

#endif // LLDP_PACKET_HH