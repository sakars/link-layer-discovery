
#include "system_information.hh"
#include "netlink.hh"

#include <iomanip>
#include <iostream>

int main()
{
    // std::cout << "Device ID: " << ndisc::GetMachineId() << '\n';
    // for (const auto &ethernet_device : ndisc::GetEthernetDeviceList())
    // {
    //     std::cout << ethernet_device;
    //     const auto MAC = ndisc::GetDeviceMacAddress(ethernet_device);

    //     std::cout << " Mac: "
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[0]) << ":"
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[1]) << ":"
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[2]) << ":"
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[3]) << ":"
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[4]) << ":"
    //               << std::setfill('0') << std::setw(2)
    //               << std::hex << static_cast<int>(MAC.ether_addr_octet[5]);

    //     std::cout << '\n';
    // }

    ndisc::NetlinkSocket socket;
    std::cout << std::boolalpha << socket.IsSocketOk() << '\n';

    for (const auto &interface : socket.GetDevices())
    {
        std::cout << interface.interface_index << "\t";
        for (const auto &byte : interface.mac_address.value())
        {
            std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)byte << " ";
        }
        std::cout << interface.interface_name.value_or("") << "\n";
    }
}
