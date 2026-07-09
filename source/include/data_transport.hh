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
    const std::string SOCKET_PATH = "/run/ndisc/ndisc.sock";

    struct ChassisEntry
    {
        uint16_t chassis_id;
        uint16_t name_length;
        std::array<uint8_t, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t)> name;
    };
    static_assert(std::is_trivially_copyable_v<ChassisEntry>);
    static_assert(sizeof(ChassisEntry) <= DATA_MAX_SIZE);

    struct NeighbourEntry
    {
        uint16_t chassis_id;
        uint16_t neighbour_id;
        uint16_t port_size;
        std::array<uint8_t, DATA_MAX_SIZE - sizeof(uint16_t) - sizeof(uint16_t) - sizeof(uint16_t)> neighbour_port;
    };
    static_assert(std::is_trivially_copyable_v<NeighbourEntry>);
    static_assert(sizeof(NeighbourEntry) <= DATA_MAX_SIZE);

    struct IpEntry
    {
        uint16_t neighbour_id;
        std::array<uint8_t, sizeof(in_addr)> address;
    };
    static_assert(std::is_trivially_copyable_v<IpEntry>);
    static_assert(sizeof(IpEntry) <= DATA_MAX_SIZE);

    struct DataTransportPacket
    {
        uint16_t request_id = 0;
        NeighbourDataType type = END_OF_DATA;
        std::array<uint8_t, DATA_MAX_SIZE> data{};

        DataTransportPacket() {
        };

        DataTransportPacket(uint16_t rid, IpEntry &entry) : request_id(rid), type(IP_ENTRY)
        {
            std::span<uint8_t> entry_data_span = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&entry), sizeof(IpEntry));
            std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
        }

        DataTransportPacket(uint16_t rid, NeighbourEntry &entry) : request_id(rid), type(IP_ENTRY)
        {
            std::span<uint8_t> entry_data_span = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&entry), sizeof(NeighbourEntry));
            std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
        }

        DataTransportPacket(uint16_t rid, ChassisEntry &entry) : request_id(rid), type(IP_ENTRY)
        {
            std::span<uint8_t> entry_data_span = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&entry), sizeof(ChassisEntry));
            std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
        }
    };
    static_assert(std::is_trivially_copyable_v<DataTransportPacket>);

    void sendData(int file_descriptor, DataTransportPacket data_transport)
    {
        iovec iov{
            .iov_base = &data_transport,
            .iov_len = sizeof(data_transport),
        };
        msghdr header{
            .msg_name = nullptr,
            .msg_namelen = 0,
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };

        sendmsg(file_descriptor, &header, 0);
    }

    std::expected<DataTransportPacket, int> readData(int file_descriptor)
    {
        DataTransportPacket data_transport;
        iovec iov{
            .iov_base = &data_transport,
            .iov_len = sizeof(data_transport)};
        msghdr header{
            .msg_name = nullptr,
            .msg_namelen = 0,
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };

        ssize_t bytes_sent = recvmsg(file_descriptor, &header, MSG_DONTWAIT);
        if (bytes_sent < 0)
        {
            return std::unexpected(errno);
        }
        if (bytes_sent < static_cast<ssize_t>(sizeof(DataTransportPacket)))
        {
            return std::unexpected(bytes_sent);
        }
        return data_transport;
    }

    void prepareDirectory()
    {
        std::filesystem::path path = SOCKET_PATH;
        std::filesystem::create_directories(path.remove_filename());
        unlink(SOCKET_PATH.c_str());
    }

    std::expected<int, int> createDataSocket()
    {
        prepareDirectory();
        int listen_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (listen_socket < 0)
        {
            return std::unexpected(errno);
        }

        sockaddr_un unix_address{
            .sun_family = AF_UNIX,
            .sun_path = {},
        };
        std::copy(std::begin(SOCKET_PATH), std::end(SOCKET_PATH), std::begin(unix_address.sun_path));
        int bind_result = bind(listen_socket, reinterpret_cast<const sockaddr *>(&unix_address), sizeof(unix_address));
        if (bind_result != 0)
        {
            close(listen_socket);
            return std::unexpected(errno);
        }

        int listen_result = listen(listen_socket, 1);
        if (listen_result != 0)
        {
            close(listen_socket);
            return std::unexpected(errno);
        }

        return listen_socket;
    }

    class DataTransportSocket : public EventHandler
    {
        int socket_;
        std::reference_wrapper<DeviceRepository> device_repository_;
        std::reference_wrapper<NeighbourList> neighbour_list_;
        int notify_socket_;
        bool eof_received_ = false;

    public:
        DataTransportSocket(int socket,
                            DeviceRepository &device_repository,
                            NeighbourList &neighbour_list,
                            int notify_fd) : socket_(socket),
                                             device_repository_(device_repository),
                                             neighbour_list_(neighbour_list),
                                             notify_socket_(notify_fd)
        {
        }

        bool EofReceived() const
        {
            return eof_received_;
        }

        void Call() override
        {
            uint16_t request_id = 0;
            iovec iov{
                .iov_base = &request_id,
                .iov_len = sizeof(request_id),
            };
            msghdr msg{
                .msg_name = nullptr,
                .msg_namelen = 0,
                .msg_iov = &iov,
                .msg_iovlen = 1,
                .msg_control = nullptr,
                .msg_controllen = 0,
                .msg_flags = 0,

            };

            ssize_t received_bytes = recvmsg(socket_, &msg, MSG_TRUNC);
            if (received_bytes < 0)
            {
                std::cerr << "Data transport socket failed to receive messages\n";
                return;
            }
            if (received_bytes == 0)
            {
                eof_received_ = true;
                uint64_t value = 1;
                write(notify_socket_, &value, sizeof(value));
            }
            if (received_bytes != sizeof(request_id))
            {
                std::cerr << "Data transport socket received invalid datagram\n";
                return;
            }
            DumpData(request_id);
        }

        void DumpData(uint16_t request_id)
        {
            uint16_t chassis_id_counter = 0;
            uint16_t neighbour_id_counter = 0;
            for (const auto &[chassis_id, port_map] : neighbour_list_.get().chassis_map)
            {
                ChassisEntry entry{
                    .chassis_id = chassis_id_counter,
                    .name_length = 0,
                    .name = {},
                };
                if (chassis_id.size() > entry.name.size())
                {
                    std::cerr << "Chassis ID has invalid size\n";
                    continue;
                }
                std::copy(chassis_id.begin(), chassis_id.end(), entry.name.begin());
                entry.name_length = chassis_id.size();
                DataTransportPacket dtp = DataTransportPacket(request_id, entry);
                iovec iov{
                    .iov_base = &dtp,
                    .iov_len = sizeof(dtp)};
                msghdr header{
                    .msg_name = nullptr,
                    .msg_namelen = 0,
                    .msg_iov = &iov,
                    .msg_iovlen = 1,
                    .msg_control = nullptr,
                    .msg_controllen = 0,
                    .msg_flags = 0,
                };
                sendmsg(socket_, &header, 0);

                for (const auto &[port_id, neighbour] : port_map)
                {
                    NeighbourEntry entry{
                        .chassis_id = chassis_id_counter,
                        .neighbour_id = neighbour_id_counter,
                        .port_size = 0,
                        .neighbour_port = {},
                    };
                    if (port_id.size() > entry.neighbour_port.size())
                    {
                        std::cerr << "Port size larger than can deliver, not broadcasted over to client\n";
                    }
                    entry.port_size = port_id.size();
                    std::copy(port_id.begin(), port_id.end(), entry.neighbour_port.begin());
                    dtp = DataTransportPacket(request_id, entry);
                    sendmsg(socket_, &header, 0);
                    if (neighbour.ip_address.has_value())
                    {
                        IpEntry entry{
                            .neighbour_id = neighbour_id_counter,
                            .address = neighbour.ip_address.value(),
                        };
                        dtp = DataTransportPacket(request_id, entry);
                        sendmsg(socket_, &header, 0);
                    }
                    neighbour_id_counter++;
                }
                chassis_id_counter++;
            }
        }

        int GetSocket() const override
        {
            return socket_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
    };

    class DataTransportRepository : public EventHandler
    {
        int socket_;
        std::vector<std::shared_ptr<DataTransportSocket>> transport_sockets_;

        DataTransportRepository(int socket) : socket_(socket) {}

    public:
        DataTransportRepository(DataTransportRepository &) = delete;
        DataTransportRepository(DataTransportRepository &&other) : socket_(other.socket_), transport_sockets_(std::move(other.transport_sockets_))
        {
            other.socket_ = -1;
        }

        DataTransportRepository &operator=(DataTransportRepository &) = delete;
        DataTransportRepository &operator=(DataTransportRepository &&other)
        {
            socket_ = other.socket_;
            transport_sockets_ = std::move(other.transport_sockets_);
            other.socket_ = -1;
            return *this;
        }

        ~DataTransportRepository() override
        {
            if (socket_ >= 0)
            {
                close(socket_);
            }
        }

        static std::expected<std::unique_ptr<DataTransportRepository>, int> Create()
        {
            int socket = eventfd(0, 0);
            if (socket == -1)
            {
                return std::unexpected(errno);
            }
            return std::make_unique<DataTransportRepository>(DataTransportRepository(socket));
        }

        void Call() override
        {
            uint64_t value = 0;
            ssize_t result = read(socket_, &value, sizeof(value));
            if (result < 0)
            {
                std::cerr << "Failed to read DataTransportRepository eventfd\n";
            }
            for (size_t i = transport_sockets_.size(); i > 0; i--)
            {
                size_t idx = i - 1;

                if (transport_sockets_[idx]->EofReceived())
                {
                    transport_sockets_.erase(std::next(transport_sockets_.begin(), idx));
                }
            }
        }

        int GetSocket() const override
        {
            return socket_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
    };

    class DataTransportListenSocket : public EventHandler
    {
        int listener_socket_;
        std::reference_wrapper<DeviceRepository> device_repository_;
        std::reference_wrapper<NeighbourList> neighbour_list_;
        int notify_socket_;

        DataTransportListenSocket(int listener_socket,
                                  DeviceRepository &device_repository,
                                  NeighbourList &neighbour_list,
                                  DataTransportRepository &dtr) : listener_socket_(listener_socket),
                                                                  device_repository_(device_repository),
                                                                  neighbour_list_(neighbour_list),
                                                                  notify_socket_(dtr.GetSocket())
        {
        }

    public:
        static std::expected<std::unique_ptr<DataTransportListenSocket>, int> Create(
            DeviceRepository &device_repository,
            NeighbourList &neighbour_list,
            DataTransportRepository &dtr)
        {
            std::expected<int, int> socket = createDataSocket();
            if (!socket.has_value())
            {
                return std::unexpected(socket.error());
            }
            return std::make_unique<DataTransportListenSocket>(DataTransportListenSocket(socket.value(), device_repository, neighbour_list, dtr));
        }

        void Call() override
        {
            sockaddr_un unix_addr{};
            socklen_t size = sizeof(unix_addr);
            int accept_socket = accept(listener_socket_, reinterpret_cast<sockaddr *>(&unix_addr), &size);
            if (accept_socket < 0)
            {
                std::cerr << "Data transport accept failed with errno: " << errno << "\n";
                return;
            }
            std::shared_ptr<DataTransportSocket> dts = std::make_shared<DataTransportSocket>(DataTransportSocket(accept_socket, device_repository_, neighbour_list_, notify_socket_));
        }

        int GetSocket() const override
        {
            return listener_socket_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }
    };

} // namespace ndisc::data

#endif // DATA_TRANSPORT_HH