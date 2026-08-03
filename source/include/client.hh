#ifndef CLIENT_HH
#define CLIENT_HH

#include <array>
#include <errno.h>
#include <expected>
#include <filesystem>
#include <stdint.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "device_repository.hh"
#include "event_handlers.hh"
#include "lldp_monitor.hh"
#include "owned_file_descriptor.hh"

namespace client
{
    enum NeighbourDataType : uint8_t
    {
        END_OF_DATA = 0,
        CHASSIS_ENTRY = 1,
        NEIGHBOUR_ENTRY = 2,
        IP_ENTRY = 3,
        IPV6_ENTRY = 4,
    };

    constexpr uint16_t DATA_MAX_SIZE = 512;

    struct ChassisEntry
    {
        uint16_t chassis_id;
        uint16_t name_length;
        std::array<std::byte, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t)> name;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<ChassisEntry>);
    static_assert(sizeof(ChassisEntry) == DATA_MAX_SIZE);

    struct NeighbourEntry
    {
        uint16_t chassis_id;
        uint16_t neighbour_id;
        uint16_t port_size;
        std::array<std::byte, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t) - sizeof(uint16_t)> neighbour_port;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<NeighbourEntry>);
    static_assert(sizeof(NeighbourEntry) == DATA_MAX_SIZE);

    struct IpEntry
    {
        uint16_t neighbour_id;
        std::array<std::byte, sizeof(in_addr)> address;
        std::array<std::byte, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(std::array<std::byte, sizeof(in_addr)>)> padding;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<IpEntry>);
    static_assert(sizeof(IpEntry) == DATA_MAX_SIZE);

    struct Ipv6Entry
    {
        uint16_t neighbour_id;
        std::array<std::byte, sizeof(in6_addr)> address;
        std::array<std::byte, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(std::array<std::byte, sizeof(in6_addr)>)> padding;
    } __attribute__((packed));
    static_assert(std::is_trivially_copyable_v<Ipv6Entry>);
    static_assert(sizeof(Ipv6Entry) == DATA_MAX_SIZE);

    struct ClientPacket
    {
        uint16_t request_id = 0;
        NeighbourDataType type = END_OF_DATA;
        std::array<std::byte, DATA_MAX_SIZE> data{};

        ClientPacket();

        ClientPacket(uint16_t rid, Ipv6Entry &entry);

        ClientPacket(uint16_t rid, IpEntry &entry);

        ClientPacket(uint16_t rid, NeighbourEntry &entry);

        ClientPacket(uint16_t rid, ChassisEntry &entry);
    };
    static_assert(std::is_trivially_copyable_v<ClientPacket>);

    class ClientSenderSocket : public ndisc::EventHandler
    {
        ndisc::OwnedFileDescriptor socket_;
        ndisc::NeighbourList *neighbour_list_;
        int notify_socket_;
        bool eof_received_ = false;

    public:
        ClientSenderSocket(ndisc::OwnedFileDescriptor &&socket,
                           ndisc::NeighbourList &neighbour_list,
                           int notify_fd);

        bool EofReceived() const;

        void Call() override;

        void DumpData(uint16_t request_id);

        int GetSocket() const override;

        uint32_t GetEvents() const override;
    };

    class ClientRepository : public ndisc::EventHandler
    {
        ndisc::OwnedFileDescriptor socket_;
        std::vector<std::shared_ptr<ClientSenderSocket>> transport_sockets_;
        ndisc::EventManager *event_manager_;

        ClientRepository(ndisc::OwnedFileDescriptor &&socket, ndisc::EventManager &event_manager) : socket_(std::move(socket)), event_manager_(&event_manager) {}

    public:
        std::expected<void, int> Add(std::shared_ptr<ClientSenderSocket> dts);

        static std::expected<std::unique_ptr<ClientRepository>, int> Create(ndisc::EventManager &event_manager);

        void Call() override;

        int GetSocket() const override;

        uint32_t GetEvents() const override;
    };

    class ClientListenSocket : public ndisc::EventHandler
    {
        ndisc::DeletingOwnedFileDescriptor lock_socket_;
        ndisc::DeletingOwnedFileDescriptor listener_socket_;
        ndisc::NeighbourList *neighbour_list_;
        ClientRepository *dtr_;

        ClientListenSocket(ndisc::DeletingOwnedFileDescriptor &&lock_socket,
                           ndisc::DeletingOwnedFileDescriptor &&listener_socket,
                           ndisc::NeighbourList &neighbour_list,
                           ClientRepository &dtr);

    public:
        static std::expected<std::unique_ptr<ClientListenSocket>, int> Create(
            ndisc::NeighbourList &neighbour_list,
            ClientRepository &dtr);

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

    class ClientReceiverSocket
    {
        ndisc::OwnedFileDescriptor socket_;
        uint16_t request_id_ = 1;

        ClientReceiverSocket(ndisc::OwnedFileDescriptor &&socket_fd) : socket_(std::move(socket_fd)) {}

    public:
        static std::expected<ClientReceiverSocket, int> Create();

        struct DeviceData
        {
            std::vector<std::byte> chassis;
            std::vector<std::byte> port;
            std::optional<std::array<std::byte, sizeof(in_addr)>> ipv4_address;
            std::optional<std::array<std::byte, sizeof(in6_addr)>> ipv6_address;
        };

        std::map<uint16_t, DeviceData> GetData();
    };

} // namespace client

#endif // CLIENT_HH