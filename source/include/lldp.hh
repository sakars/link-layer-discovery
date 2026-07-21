#ifndef LLDP_HH
#define LLDP_HH

#include <stdint.h>

namespace ndisc
{
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
    } // namespace lldp
} // namespace ndisc

#endif // LLDP_HH