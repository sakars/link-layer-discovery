
#include "lldp_packet.hh"
#include "system_information.hh"

#include <array>

namespace ndisc
{

    inline uint8_t mostSignificantByte(const uint16_t VALUE)
    {
        const uint8_t BYTE_MASK = 0xFF;
        const uint8_t BYTE_SIZE = 8;
        return (VALUE >> BYTE_SIZE) & BYTE_MASK;
    }

    inline uint8_t leastSignificantByte(const uint16_t VALUE)
    {
        const uint8_t BYTE_MASK = 0xFF;
        return VALUE & BYTE_MASK;
    }

    std::vector<uint8_t> LLDPDUTypeLengthValue::toFrameBuffer() const
    {
        const size_t TYPE_BIT_OFFSET = 9;
        const size_t LENGTH = value.size();
        uint16_t header = (type << TYPE_BIT_OFFSET) & LENGTH;
        std::vector<uint8_t> buffer{};
        buffer.reserve(2 + LENGTH);
        buffer.push_back(mostSignificantByte(header));
        buffer.push_back(leastSignificantByte(header));
        buffer.insert(std::end(buffer), std::begin(value), std::end(value));
        return buffer;
    }
    std::vector<uint8_t> LLDPDataUnit::toFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        const std::vector<uint8_t> CHASSIS_ID_BUFFER = chassis_id.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(CHASSIS_ID_BUFFER), std::end(CHASSIS_ID_BUFFER));
        const std::vector<uint8_t> PORT_ID_BUFFER = port_id.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(PORT_ID_BUFFER), std::end(PORT_ID_BUFFER));
        const std::vector<uint8_t> TIME_TO_LIVE_BUFFER = time_to_live.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(TIME_TO_LIVE_BUFFER), std::end(TIME_TO_LIVE_BUFFER));
        for (const LLDPDUTypeLengthValue &tlv : optional_tlv)
        {
            const std::vector<uint8_t> TLV_BUFFER = tlv.toFrameBuffer();
            buffer.insert(std::end(buffer), std::begin(TLV_BUFFER), std::end(TLV_BUFFER));
        }
        constexpr std::array<uint8_t, 2> ARR{{0, 0}};
        buffer.insert(std::end(buffer), std::begin(ARR), std::end(ARR));
        return buffer;
    }

    std::vector<uint8_t> LLDPEthernetFrame::toFrameBuffer() const
    {
        std::vector<uint8_t> buffer{};
        buffer.insert(std::end(buffer), std::begin(header.ether_dhost), std::end(header.ether_dhost));
        buffer.insert(std::end(buffer), std::begin(header.ether_shost), std::end(header.ether_shost));
        buffer.push_back(mostSignificantByte(header.ether_type));
        buffer.push_back(leastSignificantByte(header.ether_type));
        const std::vector<uint8_t> DATA_UNIT_BUFFER = data_unit.toFrameBuffer();
        buffer.insert(std::end(buffer), std::begin(DATA_UNIT_BUFFER), std::end(DATA_UNIT_BUFFER));
        return buffer;
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
