#ifndef DATA_TRANSPORT_HH
#define DATA_TRANSPORT_HH

#include <array>
#include <errno.h>
#include <expected>
#include <filesystem>
#include <stdint.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "event_handlers.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "owned_file_descriptor.hh"

namespace ndisc::data
{
    enum NeighbourDataType : uint8_t
    {
        END_OF_DATA = 0,
        CHASSIS_ENTRY = 1,
        NEIGHBOUR_ENTRY = 2,
        IP_ENTRY = 3,
    };

    constexpr uint16_t DATA_MAX_SIZE = 512;

    struct ChassisEntry
    {
        uint16_t chassis_id;
        uint16_t name_length;
        std::array<uint8_t, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t)> name;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<ChassisEntry>);
    static_assert(sizeof(ChassisEntry) == DATA_MAX_SIZE);

    struct NeighbourEntry
    {
        uint16_t chassis_id;
        uint16_t neighbour_id;
        uint16_t port_size;
        std::array<uint8_t, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t) - sizeof(uint16_t)> neighbour_port;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<NeighbourEntry>);
    static_assert(sizeof(NeighbourEntry) == DATA_MAX_SIZE);

    struct IpEntry
    {
        uint16_t neighbour_id;
        std::array<uint8_t, sizeof(in_addr)> address;
        std::array<uint8_t, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(std::array<uint8_t, sizeof(in_addr)>)> padding;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<IpEntry>);
    static_assert(sizeof(IpEntry) == DATA_MAX_SIZE);

    struct DataTransportPacket
    {
        uint16_t request_id = 0;
        NeighbourDataType type = END_OF_DATA;
        std::array<uint8_t, DATA_MAX_SIZE> data{};

        DataTransportPacket();

        DataTransportPacket(uint16_t rid, IpEntry &entry);

        DataTransportPacket(uint16_t rid, NeighbourEntry &entry);

        DataTransportPacket(uint16_t rid, ChassisEntry &entry);
    };
    static_assert(std::is_trivially_copyable_v<DataTransportPacket>);

    class DataTransportSocket : public EventHandler
    {
        OwnedFileDescriptor socket_;
        std::reference_wrapper<DeviceRepository> device_repository_;
        std::reference_wrapper<NeighbourList> neighbour_list_;
        int notify_socket_;
        bool eof_received_ = false;

    public:
        DataTransportSocket(OwnedFileDescriptor &&socket,
                            DeviceRepository &device_repository,
                            NeighbourList &neighbour_list,
                            int notify_fd);

        bool EofReceived() const;

        void Call() override;

        void DumpData(uint16_t request_id);

        int GetSocket() const override;

        uint32_t GetEvents() const override;
    };

    class DataTransportRepository : public EventHandler
    {
        OwnedFileDescriptor socket_;
        std::vector<std::shared_ptr<DataTransportSocket>> transport_sockets_;
        std::reference_wrapper<EventManager> event_manager_;

        DataTransportRepository(OwnedFileDescriptor &&socket, EventManager &event_manager) : socket_(std::move(socket)), event_manager_(event_manager) {}

    public:
        void Add(std::shared_ptr<DataTransportSocket> dts);

        static std::expected<std::unique_ptr<DataTransportRepository>, int> Create(EventManager &event_manager);

        void Call() override;

        int GetSocket() const override;

        uint32_t GetEvents() const override;
    };

    class DataTransportListenSocket : public EventHandler
    {
        OwnedFileDescriptor listener_socket_;
        std::reference_wrapper<DeviceRepository> device_repository_;
        std::reference_wrapper<NeighbourList> neighbour_list_;
        std::reference_wrapper<DataTransportRepository> dtr_;

        DataTransportListenSocket(OwnedFileDescriptor &&listener_socket,
                                  DeviceRepository &device_repository,
                                  NeighbourList &neighbour_list,
                                  DataTransportRepository &dtr);

    public:
        static std::expected<std::unique_ptr<DataTransportListenSocket>, int> Create(
            DeviceRepository &device_repository,
            NeighbourList &neighbour_list,
            DataTransportRepository &dtr);

        void Call() override;

        int GetSocket() const override
        {
            return *listener_socket_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
    };

    class DataTransportClient
    {
        OwnedFileDescriptor socket_;
        uint16_t request_id_ = 1;

        DataTransportClient(OwnedFileDescriptor &&socket_fd) : socket_(std::move(socket_fd)) {}

    public:
        static std::expected<DataTransportClient, int> Create();

        struct DeviceData
        {
            std::vector<uint8_t> chassis;
            std::vector<uint8_t> port;
            std::optional<std::array<uint8_t, 4>> ip_address;
        };

        std::map<uint16_t, DeviceData> GetData();
    };

} // namespace ndisc::data

#endif // DATA_TRANSPORT_HH