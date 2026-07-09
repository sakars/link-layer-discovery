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

        DataTransportPacket() {
        };

        DataTransportPacket(uint16_t rid, IpEntry &entry) : request_id(rid), type(IP_ENTRY)
        {
            std::span<uint8_t> entry_data_span = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&entry), sizeof(IpEntry));
            std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
        }

        DataTransportPacket(uint16_t rid, NeighbourEntry &entry) : request_id(rid), type(NEIGHBOUR_ENTRY)
        {
            std::span<uint8_t> entry_data_span = std::span<uint8_t>(reinterpret_cast<uint8_t *>(&entry), sizeof(NeighbourEntry));
            std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
        }

        DataTransportPacket(uint16_t rid, ChassisEntry &entry) : request_id(rid), type(CHASSIS_ENTRY)
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

    void sendRequest(int file_descriptor, uint16_t request_id)
    {

        iovec iov{
            .iov_base = &request_id,
            .iov_len = sizeof(request_id),
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

    std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, std::monostate>, int> readDataPacket(int file_descriptor)
    {
        std::expected<DataTransportPacket, int> packet = readData(file_descriptor);
        if (!packet.has_value())
        {
            return std::unexpected(errno);
        }
        if (packet->type == END_OF_DATA)
        {
            return std::monostate{};
        }
        if (packet->type == CHASSIS_ENTRY)
        {
            return std::bit_cast<ChassisEntry>(packet->data);
        }
        if (packet->type == NEIGHBOUR_ENTRY)
        {
            return std::bit_cast<NeighbourEntry>(packet->data);
        }
        if (packet->type == IP_ENTRY)
        {
            return std::bit_cast<IpEntry>(packet->data);
        }

        return std::unexpected(0);
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
                std::cerr << "Data transport socket failed to receive messages, errno " << errno << "\n";
                return;
            }
            if (received_bytes == 0)
            {
                std::cout << "EoF received\n";
                eof_received_ = true;
                uint64_t value = 1;
                write(notify_socket_, &value, sizeof(value));
                return;
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
            DataTransportPacket dtp = DataTransportPacket();
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
                std::cout << "Chassis length: " << chassis_id.size();
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
                            .padding = {},
                        };
                        dtp = DataTransportPacket(request_id, entry);
                        sendmsg(socket_, &header, 0);
                    }
                    neighbour_id_counter++;
                }
                chassis_id_counter++;
            }
            dtp = DataTransportPacket();
            sendmsg(socket_, &header, 0);
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
        std::vector<std::shared_ptr<DataTransportSocket>> transport_sockets_{};
        std::reference_wrapper<EventManager> event_manager_;

        DataTransportRepository(int socket, EventManager &event_manager) : socket_(socket), event_manager_(event_manager) {}

    public:
        DataTransportRepository(DataTransportRepository &) = delete;
        DataTransportRepository(DataTransportRepository &&other) : socket_(other.socket_), transport_sockets_(std::move(other.transport_sockets_)), event_manager_(other.event_manager_)
        {
            other.socket_ = -1;
        }

        DataTransportRepository &operator=(DataTransportRepository &) = delete;
        DataTransportRepository &operator=(DataTransportRepository &&other)
        {
            socket_ = other.socket_;
            transport_sockets_ = std::move(other.transport_sockets_);
            event_manager_ = other.event_manager_;
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

        void Add(std::shared_ptr<DataTransportSocket> dts)
        {
            event_manager_.get().Add(dts);
            transport_sockets_.push_back(std::move(dts));
        }

        static std::expected<std::unique_ptr<DataTransportRepository>, int> Create(EventManager &event_manager)
        {
            int socket = eventfd(0, 0);
            if (socket == -1)
            {
                return std::unexpected(errno);
            }
            return std::make_unique<DataTransportRepository>(DataTransportRepository(socket, event_manager));
        }

        void Call() override
        {
            uint64_t value = 0;
            ssize_t result = read(socket_, &value, sizeof(value));
            std::cout << "Read Data Transport Request " << result << "\n";
            if (result < 0)
            {
                std::cerr << "Failed to read DataTransportRepository eventfd\n";
            }
            std::cout << "Value " << value << "\n";
            for (size_t i = transport_sockets_.size(); i > 0; i--)
            {
                size_t idx = i - 1;

                if (transport_sockets_[idx]->EofReceived())
                {
                    std::cout << "Eof received on socket, erasing...\n";
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
        std::reference_wrapper<DataTransportRepository> dtr_;

        DataTransportListenSocket(int listener_socket,
                                  DeviceRepository &device_repository,
                                  NeighbourList &neighbour_list,
                                  DataTransportRepository &dtr) : listener_socket_(listener_socket),
                                                                  device_repository_(device_repository),
                                                                  neighbour_list_(neighbour_list),
                                                                  dtr_(dtr)

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
            std::cout << "New data connection...\n";
            sockaddr_un unix_addr{};
            socklen_t size = sizeof(unix_addr);
            int accept_socket = accept(listener_socket_, reinterpret_cast<sockaddr *>(&unix_addr), &size);
            if (accept_socket < 0)
            {
                std::cerr << "Data transport accept failed with errno: " << errno << "\n";
                return;
            }
            std::shared_ptr<DataTransportSocket> dts = std::make_shared<DataTransportSocket>(DataTransportSocket(accept_socket, device_repository_, neighbour_list_, dtr_.get().GetSocket()));
            dtr_.get().Add(std::move(dts));
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

    class DataTransportClient
    {
        int socket_;
        uint16_t request_id_ = 1;

        DataTransportClient(int socket_fd) : socket_(socket_fd) {}

    public:
        DataTransportClient(const DataTransportClient &) = delete;
        DataTransportClient(DataTransportClient &&other) noexcept : socket_(other.socket_)
        {
            other.socket_ = -1;
        }
        DataTransportClient &operator=(const DataTransportClient &) = delete;
        DataTransportClient &operator=(DataTransportClient &&other) noexcept
        {
            if (socket_ >= 0)
            {
                close(socket_);
            }
            socket_ = other.socket_;
            return *this;
        }

        ~DataTransportClient()
        {
            if (socket_ >= 0)
            {
                close(socket_);
            }
        }

        static std::expected<DataTransportClient, int> Create()
        {
            int socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (socket_fd < 0)
            {
                return std::unexpected(errno);
            }
            sockaddr_un address{
                .sun_family = AF_UNIX,
                .sun_path = {},
            };

            std::copy(std::begin(SOCKET_PATH), std::end(SOCKET_PATH), std::begin(address.sun_path));
            int bind_result = connect(socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
            if (bind_result < 0)
            {
                return std::unexpected(errno);
            }
            return DataTransportClient(socket_fd);
        }

        struct DeviceData
        {
            std::vector<uint8_t> chassis{};
            std::vector<uint8_t> port{};
            std::optional<std::array<uint8_t, 4>> ip_address{};
        };

        std::map<uint16_t, DeviceData> GetData()
        {
            std::map<uint16_t, std::vector<uint8_t>> chassis_map{};
            std::map<uint16_t, DeviceData> map{};
            std::cout << "Sending request " << request_id_ << "\n";
            sendRequest(socket_, request_id_);
            int i = 0;
            while (true)
            {
                i++;
                if (i > 50)
                {
                    return {};
                }
                std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, std::monostate>, int> packet = readDataPacket(socket_);
                if (!packet.has_value())
                {
                    if (packet.error() == EAGAIN)
                    {
                        continue;
                    }
                    std::cerr << "Failed to read packet, errno: " << packet.error() << "\n";
                    break;
                }
                if (ChassisEntry *chassis = std::get_if<ChassisEntry>(&*packet))
                {
                    chassis_map[chassis->chassis_id] = std::vector<uint8_t>(chassis->name.begin(), std::next(chassis->name.begin(), chassis->name_length));
                }
                else if (NeighbourEntry *neighbour = std::get_if<NeighbourEntry>(&*packet))
                {
                    std::vector<uint8_t> &chassis = chassis_map[neighbour->chassis_id];
                    std::vector<uint8_t> port = std::vector<uint8_t>(neighbour->neighbour_port.begin(), std::next(neighbour->neighbour_port.begin(), neighbour->port_size));
                    map[neighbour->neighbour_id] = DeviceData{
                        .chassis = chassis,
                        .port = port,
                        .ip_address = std::nullopt,
                    };
                }
                else if (IpEntry *ip_entry = std::get_if<IpEntry>(&*packet))
                {
                    map[ip_entry->neighbour_id].ip_address = ip_entry->address;
                }
                else
                {
                    break;
                }
            }
            request_id_++;
            return map;
        }
    };

} // namespace ndisc::data

#endif // DATA_TRANSPORT_HH