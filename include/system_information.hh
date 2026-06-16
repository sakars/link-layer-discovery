#ifndef NDISC_SYSTEM_INFORMATION_HH
#define NDISC_SYSTEM_INFORMATION_HH
#include <vector>
#include <string>
#include <net/ethernet.h>

namespace ndisc
{
    std::vector<std::string> GetEthernetDeviceList();

    std::string GetMachineId();

    ether_addr GetDeviceMacAddress(const std::string &device);

}
#endif // NDISC_SYSTEM_INFORMATION_HH
