#ifndef LLDP_HH
#define LLDP_HH

#include <cstddef>
#include <stdint.h>

namespace lldp
{
    constexpr uint8_t TYPE_BIT_OFFSET = 9;
    constexpr uint16_t TYPE_MASK = (1 << TYPE_BIT_OFFSET) - 1;
    constexpr uint8_t ETHERTYPE_SIZE = sizeof(uint16_t);

    enum TLV : uint8_t
    {
        END_OF_LLDPDU = 0,
        CHASSIS_ID = 1,
        PORT_ID = 2,
        TIME_TO_LIVE = 3,
        PORT_DESCRIPTION = 4,
        SYSTEM_NAME = 5,
        SYSTEM_DESCRIPTION = 6,
        SYSTEM_CAPABILITIES = 7,
        MANAGEMENT_ADDRESS = 8,
        ORGANIZATIONALLY_SPECIFIC = 127,
    };

    constexpr std::byte PORT_ID_MAC_TYPE{0x03};

    // https://www.iana.org/assignments/address-family-numbers/address-family-numbers.xhtml
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4 = std::byte{1};
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6 = std::byte{2};
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC = std::byte{6};

    constexpr std::byte MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX = std::byte{2};
} // namespace lldp

#endif // LLDP_HH