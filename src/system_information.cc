
#include "system_information.hh"

#include <filesystem>
#include <fstream>
#include <linux/if_arp.h>
#include <unistd.h>

namespace ndisc
{
    std::vector<std::string> GetEthernetDeviceList()
    {
        std::vector<std::string> ethernet_device_list;
        const std::string NETWORK_PATH = "/sys/class/net";
        const std::filesystem::directory_iterator NETWORK_DEVICE_ITERATOR = std::filesystem::directory_iterator(NETWORK_PATH);
        for (const std::filesystem::directory_entry &entry : NETWORK_DEVICE_ITERATOR)
        {
            if (!entry.is_directory())
            {
                continue;
            }
            const std::filesystem::path &device_path = entry.path();
            const std::filesystem::path TYPE_PATH = device_path / "type";
            if (!std::filesystem::exists(TYPE_PATH))
            {
                continue;
            }
            std::ifstream type_file(TYPE_PATH);
            int device_type = 0;
            type_file >> device_type;
            type_file.close();
            if (device_type != ARPHRD_ETHER)
            {
                continue;
            }
            ethernet_device_list.push_back(device_path.filename());
        }
        return ethernet_device_list;
    }

    std::string GetMachineId()
    {
        std::ifstream machine_id_file_stream("/etc/machine-id");
        std::string machine_id;
        machine_id_file_stream >> machine_id;
        machine_id_file_stream.close();
        return machine_id;
    }

    ether_addr GetDeviceMacAddress(const std::string &device)
    {
        ether_addr output{};
        const std::filesystem::path DEVICE_ADDRESS_PATH = std::filesystem::path("/sys/class/net") / device / "address";
        std::ifstream device_address_stream(DEVICE_ADDRESS_PATH);
        std::string raw_mac_address;
        device_address_stream >> raw_mac_address;
        // TODO: fscanf?
        std::sscanf(
            raw_mac_address.c_str(),
            "%hhX:%hhX:%hhX:%hhX:%hhX:%hhX",
            &output.ether_addr_octet[0],
            &output.ether_addr_octet[1],
            &output.ether_addr_octet[2],
            &output.ether_addr_octet[3],
            &output.ether_addr_octet[4],
            &output.ether_addr_octet[5]);
        return output;
    }

    std::string getDeviceIp(const std::string & /*unused*/)
    {
        // struct NetLinkRequest
        // {
        //     nlmsghdr message_header;
        //     ifinfomsg link_level_information;
        //     char attrbuf[512];
        // };

        // NetLinkRequest request{};
        // memset(&request, 0, sizeof(request));
        // request.message_header.nlmsg_len = NLMSG_LENGTH(sizeof(request.link_level_information));

        // rtattr *rta = nullptr;

        // int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        // close(fd);
        return "";
    }
} // namespace ndisc
