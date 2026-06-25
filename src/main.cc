
#include "system_information.hh"
#include "netlink_monitor.hh"
#include "scheduler.hh"
#include "lldp_monitor.hh"

#include <iomanip>
#include <iostream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <queue>

using namespace std::chrono_literals;

void printPacketType(ndisc::NetlinkPacketView packet)
{
    std::cout << "printPacketType ";
    std::cout << packet.index() << "\n";
}

auto packetConverter(std::function<void(ndisc::NetlinkPacketView)> callback)
{
    return [callback](std::span<uint8_t> packet) -> void
    {
        callback(ndisc::packetViewParser(packet));
    };
}

enum class ReaderState
{
    IDLE,
    READING,
    ERRORED,
};

// TODO: Prevent type erasure on NetlinkSockets
struct DeviceRepository
{
    std::map<unsigned int, ndisc::DeviceData> devices;

    std::optional<unsigned int> device_sequence_number = std::nullopt;
    ReaderState device_reader_state = ReaderState::IDLE;
    std::chrono::time_point<std::chrono::steady_clock> scheduled_link_dump = std::chrono::steady_clock::now();

    ReaderState ip_reader_state = ReaderState::IDLE;
    std::chrono::time_point<std::chrono::steady_clock> scheduled_addr_dump = std::chrono::steady_clock::now() + 2min;
    std::optional<unsigned int> ip_sequence_number = std::nullopt;

    std::unique_ptr<ndisc::NetlinkSocket> monitor;
    std::unique_ptr<ndisc::NetlinkSocket> device_reader;
    std::unique_ptr<ndisc::NetlinkSocket> ip_reader;
};

void logDevice(const ndisc::DeviceData &device)
{
    std::ios_base::fmtflags f(std::cout.flags());
    std::cout << "If ";
    if (device.mac_address.has_value())
    {
        std::ios_base::fmtflags f(std::cout.flags());
        std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)device.mac_address.value()[0] << std::dec << std::setfill(' ');
        for (int i = 1; i < 6; i++)
        {
            std::cout << ":" << std::setfill('0') << std::setw(2) << std::hex << (int)device.mac_address.value()[i] << std::dec << std::setfill(' ');
        }
        std::cout.flags(f);
    }
    else
    {
        std::cout << "XX:XX:XX:XX:XX:XX";
    }
    std::cout << "\t";
    if (device.ip_address.has_value())
    {

        std::ios_base::fmtflags f(std::cout.flags());
        std::cout << std::setfill(' ') << std::setw(3) << (int)device.ip_address.value()[0];
        for (int i = 1; i < 4; i++)
        {
            std::cout << "." << std::setfill(' ') << std::setw(3) << (int)device.ip_address.value()[i];
        }
        std::cout.flags(f);
    }
    else
    {
        std::cout << "XXX.XXX.XXX.XXX";
    }
    std::cout.flags(f);
    std::cout << "\t";
    if (device.interface_name.has_value())
    {
        std::cout << "Name length: " << device.interface_name.value().size() << " Name: " << device.interface_name.value();
    }
    else
    {
        std::cout << "Name: XXXX";
    }
    std::cout << "\n";
}

void tryDeviceUpdateDispatch(DeviceRepository &repository)
{
    if (std::chrono::steady_clock::now() > repository.scheduled_link_dump)
    {
        std::cout << "Scheduled link dump\n";
        repository.device_reader->SendGetLinkDumpMessage();
        repository.device_sequence_number = repository.device_reader->GetSequenceNumber();
        repository.device_reader_state = ReaderState::READING;
        repository.scheduled_link_dump = std::chrono::steady_clock::now() + 2min;
        repository.devices.clear();
    }
}

void tryIpUpdateDispatch(DeviceRepository &repository)
{
    if (std::chrono::steady_clock::now() > repository.scheduled_addr_dump)
    {
        std::cout << "Scheduled address dump\n";
        repository.ip_reader->SendGetAddrMessage();
        repository.ip_sequence_number = repository.ip_reader->GetSequenceNumber();
        repository.ip_reader_state = ReaderState::READING;
        repository.scheduled_addr_dump = std::chrono::steady_clock::now() + 2min;
        // unsigned int index = repository.ip_query_queue.front();
        // repository.ip_query_queue.pop();
        // std::cout << "Fetching ip of device " << index << "\n";
        // repository.ip_reader->SendGetAddrMessage(index);
        // repository.ip_sequence_number = repository.ip_reader->GetSequenceNumber();
        // repository.ip_current_index = index;
        // repository.ip_resend_dump_time = std::chrono::steady_clock::now() + 1s;
        // repository.ip_reader_state = ReaderState::READING;
    }
}

void repositoryMonitor(DeviceRepository &repository)
{
    std::cout << "Found " << repository.devices.size() << " devices\n";
}

const std::array<void (*)(DeviceRepository &), 2> repository_state_managers{
    &tryDeviceUpdateDispatch,
    &tryIpUpdateDispatch,
    // &checkIpUpdateTimeout,
    // &repositoryMonitor,
};

void expiditeLinkDump(DeviceRepository &repository)
{
    repository.scheduled_link_dump = std::chrono::steady_clock::now() + 2s;
}

void expiditeAddrDump(DeviceRepository &repository)
{
    repository.scheduled_addr_dump = std::chrono::steady_clock::now() + 2s;
}

void handleMonitorPackets(DeviceRepository &repository, ndisc::NetlinkPacketView packet)
{
    if (std::get_if<ndisc::LinkView>(&packet) != nullptr || std::get_if<ndisc::AddrView>(&packet) != nullptr)
    {
        std::cout << "Expiditing Link dump\n";
        expiditeLinkDump(repository);
    }
}

void updateDeviceList(DeviceRepository &repository, ndisc::NetlinkPacketView packet)
{
    if (repository.device_reader_state != ReaderState::READING)
    {
        return;
    }
    if (!repository.device_sequence_number.has_value())
    {
        return;
    }
    unsigned int sequence_number = std::visit([&](auto packet)
                                              { return packet.header->nlmsg_seq; }, packet);
    if (sequence_number > repository.device_sequence_number.value())
    {
        std::cout << "Sequence number somehow larger\n";
    }
    if (sequence_number != repository.device_sequence_number.value())
    {
        return;
    }
    if (ndisc::LinkView *link_message = std::get_if<ndisc::LinkView>(&packet))
    {
        if (link_message->header->nlmsg_type == RTM_NEWLINK && link_message->content.interface_info->ifi_type == ARPHRD_ETHER)
        {
            int index = link_message->content.interface_info->ifi_index;
            std::cout << "New link with index: " << index << "\n";
            ndisc::DeviceData &device = repository.devices[index];

            for (const ndisc::TLVView attribute : link_message->content.attributes)
            {
                if (attribute.attribute_header->rta_type == IFLA_IFNAME)
                {
                    if (attribute.value.size() > 1)
                    {
                        device.interface_name = std::string(attribute.value.begin(), attribute.value.end());
                    }
                }
                else if (attribute.attribute_header->rta_type == IFLA_ADDRESS)
                {
                    if (attribute.value.size() == 6)
                    {
                        device.mac_address = std::array<uint8_t, 6>{};
                        std::copy(attribute.value.begin(), attribute.value.end(), device.mac_address.value().begin());
                    }
                    else
                    {

                        std::ios_base::fmtflags f(std::cout.flags());
                        std::cout << "Unexpected size for address payload " << attribute.value.size() << "\n";
                        std::cout << "Payload:";
                        for (int x : attribute.value)
                        {
                            std::cout << " " << std::setfill('0') << std::setw(2) << std::hex << x << std::dec;
                        }
                        std::cout << "\n";
                        std::cout.flags(f);
                    }
                }
            }
            logDevice(repository.devices[index]);
            expiditeAddrDump(repository);
        }
    }
    else if (std::get_if<ndisc::DoneView>(&packet))
    {
        std::cout << "Device dump complete\n";
        repository.device_sequence_number = std::nullopt;
        repository.device_reader_state = ReaderState::IDLE;
        std::cout << repository.devices.size() << " devices found\n";
    }
    else if (std::get_if<ndisc::ErrorView>(&packet))
    {
        // std::cerr << "Failed to get link dump. Retrying...\n";
        repository.device_reader_state = ReaderState::ERRORED;
    }
}

void updateAddressList(DeviceRepository &repository, ndisc::NetlinkPacketView packet)
{
    if (repository.ip_reader_state != ReaderState::READING)
    {
        return;
    }
    if (!repository.ip_sequence_number.has_value())
    {
        return;
    }
    unsigned int sequence_number = std::visit([&](auto packet)
                                              { return packet.header->nlmsg_seq; }, packet);
    std::cout << "updateAddressList: Checking sequence number\n";
    if (sequence_number > repository.ip_sequence_number.value())
    {
        std::cout << "Sequence number somehow larger\n";
    }
    if (sequence_number != repository.ip_sequence_number.value())
    {
        return;
    }
    std::cout << "Packet type: " << std::visit([&](auto packet)
                                               { return packet.header->nlmsg_type; }, packet)
              << "\n";
    if (ndisc::AddrView *link_message = std::get_if<ndisc::AddrView>(&packet))
    {
        if (link_message->header->nlmsg_type == RTM_NEWADDR)
        {
            if (link_message->content.address_info->ifa_family != AF_INET)
            {
                return;
            }
            if ((link_message->content.address_info->ifa_flags & IFA_F_SECONDARY) != 0)
            {
                return;
            }
            unsigned int index = link_message->content.address_info->ifa_index;
            for (const ndisc::TLVView attribute : link_message->content.attributes)
            {
                if (attribute.attribute_header->rta_type == IFA_ADDRESS)
                {
                    if (attribute.value.size() == 4)
                    {
                        std::cout << "Adding IP address to device with index " << index << "\n";
                        repository.devices[index].ip_address = std::array<uint8_t, 4>{};
                        std::copy(attribute.value.begin(), attribute.value.end(), repository.devices[index].ip_address.value().begin());
                        logDevice(repository.devices[index]);
                    }
                    else
                    {

                        std::ios_base::fmtflags f(std::cout.flags());
                        std::cout << "Unexpected size for address payload " << attribute.value.size() << "\n";
                        std::cout << "Payload:";
                        for (int x : attribute.value)
                        {
                            std::cout << " " << std::setfill('0') << std::setw(2) << std::hex << x << std::dec;
                        }
                        std::cout << "\n";
                        std::cout.flags(f);
                    }
                }
            }
        }
        else
        {
            std::cout << "AddrView type: " << link_message->header->nlmsg_type << "\n";
        }
    }
    else if (std::get_if<ndisc::DoneView>(&packet))
    {
        std::cout << "Address dump finished\n";
        repository.ip_sequence_number = std::nullopt;
        repository.ip_reader_state = ReaderState::IDLE;
    }
    else if (ndisc::ErrorView *error_view = std::get_if<ndisc::ErrorView>(&packet))
    {
        std::cerr << "Failed to get address dump. " << error_view->error->error << " Retrying...\n";
        repository.ip_reader_state = ReaderState::ERRORED;
        expiditeAddrDump(repository);
    }
}

struct LldpRepository
{
    std::map<unsigned int, ndisc::DeviceData> current_state;

    void MarkChangedLldpStateMachine(unsigned int idx)
    {
        std::cout << "Change for idx " << idx << "\n";
    }

    void DeleteLldpStateMachine(unsigned int idx)
    {
        std::cout << "Delete for idx " << idx << "\n";
    }

    void CreateLldpStateMachine(unsigned int idx)
    {
        std::cout << "Create for idx" << idx << "\n";
    }

    void UpdateState(std::map<unsigned int, ndisc::DeviceData> new_state)
    {
        for (auto &[index, new_device_state] : new_state)
        {
            if (current_state.contains(index))
            {
                bool anyChanged = false;
                ndisc::DeviceData &device_state = current_state.at(index);
                if (device_state.interface_name != new_device_state.interface_name)
                {
                    anyChanged = true;
                    device_state.interface_name = new_device_state.interface_name;
                }
                if (device_state.ip_address != new_device_state.ip_address)
                {
                    anyChanged = true;
                    device_state.ip_address = new_device_state.ip_address;
                }
                if (anyChanged)
                {
                    MarkChangedLldpStateMachine(index);
                }
            }
            else
            {
                current_state[index] = new_device_state;
                CreateLldpStateMachine(index);
            }
        }
        for (auto &[index, current_device_state] : current_state)
        {
            if (!new_state.contains(index))
            {
                DeleteLldpStateMachine(index);
            }
        }
    }
};

void lldpStateUpdater(LldpRepository &lldp, DeviceRepository &repository)
{
    if (
        repository.device_reader_state == ReaderState::IDLE &&
        repository.ip_reader_state == ReaderState::IDLE)
    {
        lldp.UpdateState(repository.devices);
    }
}

int main()
{
    std::optional<ndisc::EventManager> manager_create_result = ndisc::EventManager::Create();

    if (!manager_create_result.has_value())
    {
        std::cerr << "Failed to initialize Event manager.\n";
        return -1;
    }
    DeviceRepository repository;
    ndisc::EventManager manager = std::move(manager_create_result.value());
    auto multicast_socket = ndisc::NetlinkSocket::Create(packetConverter(std::bind_front(handleMonitorPackets, std::ref(repository))), RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
    manager.Add(*multicast_socket);
    repository.device_reader = ndisc::NetlinkSocket::Create(packetConverter(std::bind_front(updateDeviceList, std::ref(repository))), 0);
    if (repository.device_reader == nullptr)
    {
        std::cerr << "Failed to make device netlink socket.\n";
        return -1;
    }
    repository.ip_reader = ndisc::NetlinkSocket::Create(packetConverter(std::bind_front(updateAddressList, std::ref(repository))), 0);
    if (repository.ip_reader == nullptr)
    {
        std::cerr << "Failed to make address netlink socket.\n";
        return -1;
    }
    manager.Add(*repository.device_reader);
    manager.Add(*repository.ip_reader);

    LldpRepository lldp;
    std::chrono::time_point<std::chrono::steady_clock> last_dot_printed = std::chrono::steady_clock::now();
    while (true)
    {
        manager.Wait();
        for (const auto manager_function : repository_state_managers)
        {
            manager_function(repository);
        }
        lldpStateUpdater(lldp, repository);
        while (std::chrono::steady_clock::now() - last_dot_printed > 10s)
        {
            std::cout << ".\n";
            last_dot_printed += 10s;
        }
    }
}
