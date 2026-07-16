
#include "data_transport.hh"

#include <cstring>

namespace ndisc::data
{

    const std::string SOCKET_PATH = "/run/ndisc/ndisc.sock";

    DataTransportPacket::DataTransportPacket()
    {
    }

    DataTransportPacket::DataTransportPacket(uint16_t rid, IpEntry &entry) : request_id(rid), type(IP_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(IpEntry));
        std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
    }

    DataTransportPacket::DataTransportPacket(uint16_t rid, NeighbourEntry &entry) : request_id(rid), type(NEIGHBOUR_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(NeighbourEntry));
        std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
    }

    DataTransportPacket::DataTransportPacket(uint16_t rid, ChassisEntry &entry) : request_id(rid), type(CHASSIS_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(ChassisEntry));
        std::copy(entry_data_span.begin(), entry_data_span.end(), data.begin());
    }

    static void sendRequest(int file_descriptor, uint16_t request_id)
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

    static std::expected<DataTransportPacket, int> readData(int file_descriptor)
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

    static std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, std::monostate>, int> readDataPacket(int file_descriptor)
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

    static void prepareDirectory()
    {
        std::filesystem::path path = SOCKET_PATH;
        std::filesystem::create_directories(path.remove_filename());
        unlink(SOCKET_PATH.c_str());
    }

    static std::expected<OwnedFileDescriptor, int> createDataSocket()
    {
        prepareDirectory();
        int listen_raw_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (listen_raw_socket < 0)
        {
            return std::unexpected(errno);
        }
        OwnedFileDescriptor listen_socket{listen_raw_socket};

        sockaddr_un unix_address{
            .sun_family = AF_UNIX,
            .sun_path = {},
        };
        std::copy(std::begin(SOCKET_PATH), std::end(SOCKET_PATH), std::begin(unix_address.sun_path));
        int bind_result = bind(*listen_socket, reinterpret_cast<const sockaddr *>(&unix_address), sizeof(unix_address));
        if (bind_result != 0)
        {
            return std::unexpected(errno);
        }

        int listen_result = listen(*listen_socket, 1);
        if (listen_result != 0)
        {
            return std::unexpected(errno);
        }

        return listen_socket;
    }

    DataTransportSocket::DataTransportSocket(OwnedFileDescriptor &&socket,
                                             DeviceRepository &device_repository,
                                             NeighbourList &neighbour_list,
                                             int notify_fd) : socket_(std::move(socket)),
                                                              device_repository_(device_repository),
                                                              neighbour_list_(neighbour_list),
                                                              notify_socket_(notify_fd)
    {
    }

    bool DataTransportSocket::EofReceived() const
    {
        return eof_received_;
    }

    void DataTransportSocket::Call()
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

        ssize_t received_bytes = recvmsg(*socket_, &msg, MSG_TRUNC);
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

    void DataTransportSocket::DumpData(uint16_t request_id)
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
            std::memcpy(entry.name.begin(), chassis_id.data(), chassis_id.size());
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
            sendmsg(*socket_, &header, 0);

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
                // std::copy(port_id.begin(), port_id.end(), entry.neighbour_port.begin());
                std::memcpy(entry.neighbour_port.data(), port_id.data(), port_id.size());
                dtp = DataTransportPacket(request_id, entry);
                sendmsg(*socket_, &header, 0);
                if (neighbour.ip_address.has_value())
                {
                    IpEntry entry{
                        .neighbour_id = neighbour_id_counter,
                        .address = neighbour.ip_address.value(),
                        .padding = {},
                    };
                    dtp = DataTransportPacket(request_id, entry);
                    sendmsg(*socket_, &header, 0);
                }
                neighbour_id_counter++;
            }
            chassis_id_counter++;
        }
        dtp = DataTransportPacket();
        sendmsg(*socket_, &header, 0);
    }

    int DataTransportSocket::GetSocket() const
    {
        return *socket_;
    }

    uint32_t DataTransportSocket::GetEvents() const
    {
        return EPOLLIN;
    }

    std::expected<std::unique_ptr<DataTransportRepository>, int> DataTransportRepository::Create(EventManager &event_manager)
    {
        OwnedFileDescriptor socket = eventfd(0, 0);
        if (!socket.IsValid())
        {
            return std::unexpected(errno);
        }
        return std::make_unique<DataTransportRepository>(DataTransportRepository(std::move(socket), event_manager));
    }

    void DataTransportRepository::Call()
    {
        uint64_t value = 0;
        ssize_t result = read(*socket_, &value, sizeof(value));
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

    DataTransportListenSocket::DataTransportListenSocket(OwnedFileDescriptor &&listener_socket,
                                                         DeviceRepository &device_repository,
                                                         NeighbourList &neighbour_list,
                                                         DataTransportRepository &dtr) : listener_socket_(std::move(listener_socket)),
                                                                                         device_repository_(device_repository),
                                                                                         neighbour_list_(neighbour_list),
                                                                                         dtr_(dtr)

    {
    }

    void DataTransportRepository::Add(std::shared_ptr<DataTransportSocket> dts)
    {
        event_manager_.get().Add(dts);
        transport_sockets_.push_back(std::move(dts));
    }

    std::expected<std::unique_ptr<DataTransportListenSocket>, int> DataTransportListenSocket::Create(
        DeviceRepository &device_repository,
        NeighbourList &neighbour_list,
        DataTransportRepository &dtr)
    {
        std::expected<OwnedFileDescriptor, int> socket = createDataSocket();
        if (!socket.has_value())
        {
            return std::unexpected(socket.error());
        }
        return std::make_unique<DataTransportListenSocket>(DataTransportListenSocket(std::move(socket.value()), device_repository, neighbour_list, dtr));
    }

    void DataTransportListenSocket::Call()
    {
        std::cout << "New data connection...\n";
        sockaddr_un unix_addr{};
        socklen_t size = sizeof(unix_addr);
        int accept_socket = accept(*listener_socket_, reinterpret_cast<sockaddr *>(&unix_addr), &size);
        if (accept_socket < 0)
        {
            std::cerr << "Data transport accept failed with errno: " << errno << "\n";
            return;
        }
        std::shared_ptr<DataTransportSocket> dts = std::make_shared<DataTransportSocket>(DataTransportSocket(accept_socket, device_repository_, neighbour_list_, dtr_.get().GetSocket()));
        dtr_.get().Add(std::move(dts));
    }

    int DataTransportRepository::GetSocket() const
    {
        return *socket_;
    }
    uint32_t DataTransportRepository::GetEvents() const
    {
        return EPOLLIN;
    }

    std::expected<DataTransportClient, int> DataTransportClient::Create()
    {
        OwnedFileDescriptor socket_fd{socket(AF_UNIX, SOCK_SEQPACKET, 0)};
        if (!socket_fd.IsValid())
        {
            return std::unexpected(errno);
        }
        sockaddr_un address{
            .sun_family = AF_UNIX,
            .sun_path = {},
        };

        std::copy(std::begin(SOCKET_PATH), std::end(SOCKET_PATH), std::begin(address.sun_path));
        int bind_result = connect(*socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        if (bind_result < 0)
        {
            return std::unexpected(errno);
        }
        return DataTransportClient(std::move(socket_fd));
    }

    std::map<uint16_t, ndisc::data::DataTransportClient::DeviceData> DataTransportClient::GetData()
    {
        std::map<uint16_t, std::vector<std::byte>> chassis_map{};
        std::map<uint16_t, ndisc::data::DataTransportClient::DeviceData> map{};
        std::cout << "Sending request " << request_id_ << "\n";
        sendRequest(*socket_, request_id_);
        int i = 0;
        while (true)
        {
            i++;
            if (i > 50)
            {
                return {};
            }
            std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, std::monostate>, int> packet = readDataPacket(*socket_);
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
                chassis_map[chassis->chassis_id] = std::vector<std::byte>(chassis->name.begin(), std::next(chassis->name.begin(), chassis->name_length));
            }
            else if (NeighbourEntry *neighbour = std::get_if<NeighbourEntry>(&*packet))
            {
                std::vector<std::byte> &chassis = chassis_map[neighbour->chassis_id];
                std::vector<std::byte> port = std::vector<std::byte>(neighbour->neighbour_port.begin(), std::next(neighbour->neighbour_port.begin(), neighbour->port_size));
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

} // namespace ndisc::data
