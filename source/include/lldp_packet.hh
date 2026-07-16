#ifndef LLDP_PACKET_HH
#define LLDP_PACKET_HH

#include <net/ethernet.h>
#include <optional>
#include <span>
#include <stdint.h>
#include <vector>

#include "lldp.hh"

namespace ndisc
{
    struct LLDPDUTypeLengthValue
    {
        lldp::TLV type;
        std::vector<uint8_t> value;

        std::vector<uint8_t> ToFrameBuffer() const;

        static std::optional<LLDPDUTypeLengthValue> FromSpan(std::span<const uint8_t> &);
    };

    struct LLDPDataUnit
    {
        LLDPDUTypeLengthValue chassis_id;
        LLDPDUTypeLengthValue port_id;
        LLDPDUTypeLengthValue time_to_live;
        std::vector<LLDPDUTypeLengthValue> optional_tlv;

        std::vector<uint8_t> ToFrameBuffer() const;
        static std::optional<LLDPDataUnit> FromSpan(std::span<const uint8_t>);
    };

    struct LLDPEthernetFrame
    {
        ether_header header{};
        LLDPDataUnit data_unit;

        std::vector<uint8_t> ToFrameBuffer() const;
    };

    // LLDPDUTypeLengthValue createChassisIdTLV();
} // namespace ndisc

#endif // LLDP_PACKET_HH