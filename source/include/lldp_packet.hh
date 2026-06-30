#ifndef NDISC_LLDP_PACKET_HH
#define NDISC_LLDP_PACKET_HH

#include <cstdint>
#include <net/ethernet.h>
#include <vector>
#include <span>
#include <optional>

namespace ndisc
{
    struct LLDPDUTypeLengthValue
    {
        uint8_t type;
        std::vector<uint8_t> value;

        std::vector<uint8_t> toFrameBuffer() const;

        static std::optional<LLDPDUTypeLengthValue> fromSpan(std::span<const uint8_t> &);
    };

    struct LLDPDataUnit
    {
        LLDPDUTypeLengthValue chassis_id;
        LLDPDUTypeLengthValue port_id;
        LLDPDUTypeLengthValue time_to_live;
        std::vector<LLDPDUTypeLengthValue> optional_tlv;

        std::vector<uint8_t> toFrameBuffer() const;
        static std::optional<LLDPDataUnit> fromSpan(std::span<const uint8_t>);
    };

    struct LLDPEthernetFrame
    {
        ether_header header;
        LLDPDataUnit data_unit;

        std::vector<uint8_t> toFrameBuffer() const;

        static std::optional<LLDPEthernetFrame> fromSpan(std::span<const uint8_t>);
    };

    // LLDPDUTypeLengthValue createChassisIdTLV();
}

#endif // NDISC_LLDP_PACKET_HH