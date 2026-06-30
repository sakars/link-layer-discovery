#ifndef NDISC_SYSTEM_INFORMATION_HH
#define NDISC_SYSTEM_INFORMATION_HH
#include <vector>
#include <string>
#include <net/ethernet.h>
#include <fstream>

namespace ndisc
{
    std::vector<std::string> GetEthernetDeviceList();

    ether_addr GetDeviceMacAddress(const std::string &device);

}
#endif // NDISC_SYSTEM_INFORMATION_HH
