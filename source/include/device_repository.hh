#ifndef DEVICE_REPOSITORY_HH
#define DEVICE_REPOSITORY_HH

#include <chrono>
#include <expected>
#include <map>
#include <optional>

#include "netlink_monitor.hh"

using namespace std::chrono_literals;

namespace ndisc
{

    std::function<void(std::span<std::byte>)> packetConverter(std::function<void(ndisc::NetlinkPacketView)> CALLBACK);

    enum class ReaderState : uint8_t
    {
        IDLE,
        READING,
        ERRORED,
    };

    struct DeviceReader
    {

        std::shared_ptr<ndisc::NetlinkSocket> device_reader;
        std::optional<unsigned int> device_sequence_number = std::nullopt;
        ReaderState device_reader_state = ReaderState::IDLE;
        std::chrono::time_point<std::chrono::steady_clock> scheduled_link_dump = std::chrono::steady_clock::now();

        bool devices_updated = false;

        DeviceReader() {}

        void Tick();

        void ExpediteLinkDump();

        void UpdateDeviceList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView packet);
    };

    struct IpReader
    {
        ReaderState ip_reader_state = ReaderState::IDLE;
        std::optional<unsigned int> ip_sequence_number = std::nullopt;
        std::chrono::time_point<std::chrono::steady_clock> scheduled_addr_dump = std::chrono::steady_clock::now() + 2min;
        std::shared_ptr<ndisc::NetlinkSocket> ip_reader;

        void Tick();
        void ExpediteAddrDump();

        void UpdateAddressList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView &packet);
    };

    struct DeviceRepository
    {

        std::shared_ptr<ndisc::NetlinkSocket> monitor;

        DeviceReader device_reader;
        IpReader ip_reader;
        std::map<unsigned int, ndisc::DeviceData> devices;

        static std::expected<std::unique_ptr<DeviceRepository>, int> Create(ndisc::EventManager &manager);

        void Tick();

        void HandleMonitorPackets(ndisc::NetlinkPacketView packet);

        void UpdateDeviceList(ndisc::NetlinkPacketView packet);

        void UpdateAddressList(ndisc::NetlinkPacketView &packet);
    };

} // namespace ndisc
#endif // DEVICE_REPOSITORY_HH