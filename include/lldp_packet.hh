#ifndef NDISC_LLDP_PACKET_HH
#define NDISC_LLDP_PACKET_HH

#include <cstdint>
#include <net/ethernet.h>
#include <vector>

namespace ndisc
{
    struct LLDPDUTypeLengthValue
    {
        uint8_t type;
        std::vector<uint8_t> value;

        std::vector<uint8_t> toFrameBuffer() const;
    };

    struct LLDPDataUnit
    {
        LLDPDUTypeLengthValue chassis_id;
        LLDPDUTypeLengthValue port_id;
        LLDPDUTypeLengthValue time_to_live;
        std::vector<LLDPDUTypeLengthValue> optional_tlv;

        std::vector<uint8_t> toFrameBuffer() const;
    };

    struct LLDPEthernetFrame
    {
        ether_header header;
        LLDPDataUnit data_unit;

        std::vector<uint8_t> toFrameBuffer() const;
    };

    LLDPDUTypeLengthValue createChassisIdTLV();
}

#endif // NDISC_LLDP_PACKET_HH