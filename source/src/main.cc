
#include "netlink_monitor.hh"
#include "lldp_monitor.hh"

#include <iomanip>
#include <iostream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <queue>
#include <sys/timerfd.h>

using namespace std::chrono_literals;

auto packetConverter(const std::function<void(ndisc::NetlinkPacketView)> CALLBACK)
{
    return [CALLBACK](std::span<uint8_t> packet) -> void
    {
        CALLBACK(ndisc::packetViewParser(packet));
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

void tryDeviceUpdateDispatch(DeviceRepository &repository)
{
    if (std::chrono::steady_clock::now() > repository.scheduled_link_dump)
    {
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
        repository.ip_reader->SendGetAddrMessage();
        repository.ip_sequence_number = repository.ip_reader->GetSequenceNumber();
        repository.ip_reader_state = ReaderState::READING;
        repository.scheduled_addr_dump = std::chrono::steady_clock::now() + 10s;
    }
}

const std::array<void (*)(DeviceRepository &), 2> repository_state_managers{
    &tryDeviceUpdateDispatch,
    &tryIpUpdateDispatch,
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
    if (sequence_number != repository.device_sequence_number.value())
    {
        return;
    }
    if (ndisc::LinkView *link_message = std::get_if<ndisc::LinkView>(&packet))
    {
        if (link_message->header->nlmsg_type == RTM_NEWLINK && link_message->content.interface_info->ifi_type == ARPHRD_ETHER)
        {
            int index = link_message->content.interface_info->ifi_index;
            ndisc::DeviceData &device = repository.devices[index];
            device.if_index = index;
            for (const ndisc::TLVView attribute : link_message->content.attributes)
            {
                if (attribute.attribute_header->rta_type == IFLA_IFNAME)
                {
                    if (attribute.value.size() > 1)
                    {

                        device.interface_name = std::string(attribute.value.begin(), attribute.value.end());
                        if (device.interface_name->size() > 0 && device.interface_name->back() == '\0')
                        {
                            device.interface_name->resize(device.interface_name->size() - 1);
                        }
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
                        std::ios_base::fmtflags f(std::cerr.flags());
                        std::cerr << "Unexpected size for address payload " << attribute.value.size() << "\n";
                        std::cerr << "Payload:";
                        for (int x : attribute.value)
                        {
                            std::cerr << " " << std::setfill('0') << std::setw(2) << std::hex << x << std::dec;
                        }
                        std::cerr << "\n";
                        std::cerr.flags(f);
                    }
                }
            }
            expiditeAddrDump(repository);
        }
    }
    else if (std::get_if<ndisc::DoneView>(&packet))
    {
        repository.device_sequence_number = std::nullopt;
        repository.device_reader_state = ReaderState::IDLE;
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
    if (sequence_number > repository.ip_sequence_number.value())
    {
        std::cerr << "Sequence number somehow larger\n";
    }
    if (sequence_number != repository.ip_sequence_number.value())
    {
        return;
    }
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
            repository.devices[index].if_index = index;
            for (const ndisc::TLVView attribute : link_message->content.attributes)
            {
                if (attribute.attribute_header->rta_type == IFA_ADDRESS)
                {
                    std::cout << "Address found for " << repository.devices[index].if_index << "\n";
                    if (attribute.value.size() == 4)
                    {
                        repository.devices[index].ip_address = std::array<uint8_t, 4>{};
                        std::copy(attribute.value.begin(), attribute.value.end(), repository.devices[index].ip_address.value().begin());
                    }
                }
            }
        }
    }
    else if (std::get_if<ndisc::DoneView>(&packet))
    {
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

    void CheckSocketForTxReady(unsigned int idx)
    {
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            if (!device_data.lldp_sender.has_value() && device_data.interface_name.has_value())
            {
                // device_data.lldp_sender = ndisc::LldpSender::Create();
                std::optional<ndisc::LldpSender> sender = ndisc::LldpSender::Create();
                if (sender.has_value())
                {
                    device_data.lldp_sender = std::move(*sender);
                }
            }
        }
    }

    void MarkChangedLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].LocalChangeDetected();
        }
    }

    void DeleteLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].EndTransmission();
        }
        current_state.erase(idx);
    }

    void CreateLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            device_data.NewNeighbour();
        }
    }

    void UpdateState(std::map<unsigned int, ndisc::DeviceData> &new_state)
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
                current_state[index] = std::move(new_device_state);
                CreateLldpStateMachine(index);
            }
        }
        std::vector<unsigned int> deletables{};
        for (auto &[index, current_device_state] : current_state)
        {
            if (!new_state.contains(index))
            {
                deletables.push_back(index);
            }
        }
        for (const unsigned int idx : deletables)
        {
            DeleteLldpStateMachine(idx);
        }
    }

    void Tick()
    {
        for (auto &[idx, device] : current_state)
        {
            device.Tick();
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

class ClockHandler final : public ndisc::EventHandler
{
    int socket_fd_;
    ndisc::NeighbourList *neighbour_list_;
    DeviceRepository *device_repository_;
    LldpRepository *lldp_repository_;

    uint16_t dump_timer_;

    ClockHandler(int socket_fd, ndisc::NeighbourList *nl, DeviceRepository *dr, LldpRepository *lldp) : socket_fd_(socket_fd),
                                                                                                        neighbour_list_(nl),
                                                                                                        device_repository_(dr),
                                                                                                        lldp_repository_(lldp),
                                                                                                        dump_timer_(0) {}

public:
    void Call() override
    {
        uint64_t times_triggered = 0;
        ssize_t bytes_received = read(socket_fd_, &times_triggered, sizeof(times_triggered));
        if (bytes_received < 0 || times_triggered == 0)
        {
            return;
        }
        std::vector<std::tuple<std::vector<uint8_t>, std::vector<uint8_t>>> timed_out_entries{};
        for (auto &[chassis, port_map] : neighbour_list_->chassis_map)
        {
            for (auto &[port, entry] : port_map)
            {
                if (entry.time_to_live <= times_triggered)
                {
                    entry.time_to_live = 0;
                    timed_out_entries.emplace_back(chassis, port);
                }
                else
                {
                    entry.time_to_live -= times_triggered;
                }
            }
        }
        for (const auto &[chassis, port] : timed_out_entries)
        {
            neighbour_list_->chassis_map[chassis].erase(port);
            if (neighbour_list_->chassis_map[chassis].empty())
            {
                neighbour_list_->chassis_map.erase(chassis);
            }
        }
        tryDeviceUpdateDispatch(*device_repository_);
        tryIpUpdateDispatch(*device_repository_);
        lldpStateUpdater(*lldp_repository_, *device_repository_);
        lldp_repository_->Tick();
        if (dump_timer_ == 0)
        {
            dump_timer_ = 1;
            std::cout << "\033[2J";
            std::cout << "Device data:\n";
            std::cout << std::left
                      << std::setw(4) << "IDX"
                      << std::setw(24) << "Name"
                      << std::setw(18) << "MAC"
                      << std::setw(16) << "IP"
                      << '\n';
            for (const auto &[idx, device] : device_repository_->devices)
            {
                std::cout << std::left << std::setw(4) << device.if_index
                          << std::setw(24) << device.interface_name.value_or("---");
                if (device.mac_address.has_value())
                {
                    std::cout << std::right << std::hex << std::setfill('0')
                              << std::setw(2) << (uint16_t)(*device.mac_address)[0] << ":"
                              << std::setw(2) << (uint16_t)(*device.mac_address)[1] << ":"
                              << std::setw(2) << (uint16_t)(*device.mac_address)[2] << ":"
                              << std::setw(2) << (uint16_t)(*device.mac_address)[3] << ":"
                              << std::setw(2) << (uint16_t)(*device.mac_address)[4] << ":"
                              << std::setw(2) << (uint16_t)(*device.mac_address)[5] << " "
                              << std::dec << std::setfill(' ');
                }
                else
                {
                    std::cout << std::setw(18) << "";
                }
                if (device.ip_address.has_value())
                {
                    std::cout << std::left
                              << (uint16_t)(*device.ip_address)[0] << '.'
                              << (uint16_t)(*device.ip_address)[1] << '.'
                              << (uint16_t)(*device.ip_address)[2] << '.'
                              << (uint16_t)(*device.ip_address)[3] << ' ';
                }
                else
                {
                    std::cout << std::left << std::setw(16) << "Unknown";
                }
                std::cout << "\n";
            }
            std::cout << "\n\n";
            std::cout << "Found " << neighbour_list_->chassis_map.size() << " chassis\n";
            std::cout << "\n\nNeighbours:\n";
            std::cout << std::left
                      << std::setw(36) << "Chassis"
                      << std::setw(18) << "Port"
                      << std::setw(16) << "IP"
                      << std::setw(12) << "TTL"
                      << '\n';

            for (const auto &[chassis_id, port_map] : neighbour_list_->chassis_map)
            {

                std::string chassis;
                chassis.resize(chassis_id.size() - 2);
                std::copy(chassis_id.begin() + 1, chassis_id.end() - 1, chassis.begin());
                std::cout << "Chassis: " << chassis << "\n";
                for (const auto &[port_id, neighbour] : port_map)
                {
                    std::cout << std::setw(36) << chassis;
                    std::cout << std::right << std::hex << std::setfill('0')
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[0] << ":"
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[1] << ":"
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[2] << ":"
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[3] << ":"
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[4] << ":"
                              << std::setw(2) << (uint16_t)(neighbour.port_id)[5] << " "
                              << std::dec << std::setfill(' ');
                    if (neighbour.ip_address.has_value())
                    {
                        std::cout << std::left
                                  << (uint16_t)(*neighbour.ip_address)[0] << '.'
                                  << (uint16_t)(*neighbour.ip_address)[1] << '.'
                                  << (uint16_t)(*neighbour.ip_address)[2] << '.'
                                  << (uint16_t)(*neighbour.ip_address)[3] << ' ';
                    }
                    else
                    {
                        std::cout << std::left << std::setw(16) << "Unknown";
                    }

                    std::cout << std::setw(12) << neighbour.time_to_live;
                    std::cout << '\n';
                }
            }
        }
        dump_timer_--;
    }

    uint32_t GetEvents() const override
    {
        return EPOLLIN;
    }

    int GetSocket() const override
    {
        return socket_fd_;
    }

    static std::unique_ptr<ClockHandler> Create(ndisc::NeighbourList &neighbour_list, DeviceRepository &device_repository, LldpRepository &lldp_repository)
    {
        int socket_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (socket_fd < 0)
        {
            return nullptr;
        }
        itimerspec timer_spec;
        timer_spec.it_value.tv_nsec = 0;
        timer_spec.it_value.tv_sec = 1;
        timer_spec.it_interval.tv_nsec = 0;
        timer_spec.it_interval.tv_sec = 1;
        timerfd_settime(socket_fd, 0, &timer_spec, nullptr);
        return std::make_unique<ClockHandler>(ClockHandler(socket_fd, &neighbour_list, &device_repository, &lldp_repository));
    }
};

#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

void handler(int sig)
{
    void *array[10];
    size_t size;

    size = backtrace(array, 10);

    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

int main()
{

    signal(SIGSEGV, handler);

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
    manager.Add(*repository.device_reader);
    repository.ip_reader = ndisc::NetlinkSocket::Create(packetConverter(std::bind_front(updateAddressList, std::ref(repository))), 0);
    if (repository.ip_reader == nullptr)
    {
        std::cerr << "Failed to make address netlink socket.\n";
        return -1;
    }
    manager.Add(*repository.ip_reader);

    ndisc::NeighbourList neighbour_list;

    std::unique_ptr<ndisc::EthernetLldpMonitor> monitor = ndisc::EthernetLldpMonitor::Create(std::bind_front(ndisc::lldpFrameParser, std::ref(neighbour_list)));
    if (monitor == nullptr)
    {
        std::cerr << "Monitor is null\n";
        return -1;
    }
    manager.Add(*monitor);

    LldpRepository lldp;

    std::unique_ptr<ClockHandler> clock = ClockHandler::Create(neighbour_list, repository, lldp);
    if (clock == nullptr)
    {
        std::cerr << "Clock is nullptr\n";
        return -1;
    }
    manager.Add(*clock);

    while (true)
    {
        manager.Wait();
    }
}
