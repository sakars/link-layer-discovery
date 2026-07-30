
#include "client.hh"

#include <cstring>
#include <fcntl.h>
#include <sys/file.h>

namespace client
{
    using ndisc::EventManager;
    using ndisc::NeighbourList;
    using ndisc::OwnedFileDescriptor;

    const std::string SOCKET_PATH = "/run/ndisc/ndisc.sock";
    const std::string SOCKET_LOCK = "/run/ndisc/ndisc.lock";

    ClientPacket::ClientPacket()
    {
    }

    ClientPacket::ClientPacket(uint16_t rid, Ipv6Entry &entry) : request_id(rid), type(IPV6_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(Ipv6Entry));
        std::ranges::copy(entry_data_span, data.begin());
    }

    ClientPacket::ClientPacket(uint16_t rid, IpEntry &entry) : request_id(rid), type(IP_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(IpEntry));
        std::ranges::copy(entry_data_span, data.begin());
    }

    ClientPacket::ClientPacket(uint16_t rid, NeighbourEntry &entry) : request_id(rid), type(NEIGHBOUR_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(NeighbourEntry));
        std::ranges::copy(entry_data_span, data.begin());
    }

    ClientPacket::ClientPacket(uint16_t rid, ChassisEntry &entry) : request_id(rid), type(CHASSIS_ENTRY)
    {
        std::span<std::byte> entry_data_span = std::span<std::byte>(reinterpret_cast<std::byte *>(&entry), sizeof(ChassisEntry));
        std::ranges::copy(entry_data_span, data.begin());
    }

    static void sendRequest(const OwnedFileDescriptor &file_descriptor, uint16_t request_id)
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

        sendmsg(*file_descriptor, &header, 0);
    }

    static std::expected<ClientPacket, int> readData(int file_descriptor)
    {
        ClientPacket data_transport;
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

        ssize_t bytes_sent = recvmsg(file_descriptor, &header, 0);
        if (bytes_sent < 0)
        {
            return std::unexpected(errno);
        }
        if (bytes_sent < static_cast<ssize_t>(sizeof(ClientPacket)))
        {
            return std::unexpected(bytes_sent);
        }
        return data_transport;
    }

    static std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, Ipv6Entry, std::monostate>, int> readDataPacket(int file_descriptor)
    {
        std::expected<ClientPacket, int> packet = readData(file_descriptor);
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
        if (packet->type == IPV6_ENTRY)
        {
            return std::bit_cast<Ipv6Entry>(packet->data);
        }

        return std::unexpected(0);
    }

    static void ensureRuntimeDirectory()
    {
        std::filesystem::path path = SOCKET_PATH;
        std::filesystem::create_directories(path.remove_filename());
    }

    static void prepareDirectory()
    {
        ensureRuntimeDirectory();
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
        std::ranges::copy(SOCKET_PATH, std::begin(unix_address.sun_path));
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

    ClientSenderSocket::ClientSenderSocket(OwnedFileDescriptor &&socket,
                                           NeighbourList &neighbour_list,
                                           int notify_fd) : socket_(std::move(socket)),
                                                            neighbour_list_(&neighbour_list),
                                                            notify_socket_(notify_fd)
    {
    }

    bool ClientSenderSocket::EofReceived() const
    {
        return eof_received_;
    }

    void ClientSenderSocket::Call()
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

    static inline void sendClientPacket(const OwnedFileDescriptor &socket, ClientPacket dtp)
    {
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
        sendmsg(*socket, &header, 0);
    }

    static inline void sendChassisEntryPacket(uint16_t request_id,
                                              const OwnedFileDescriptor &socket,
                                              uint16_t chassis_id_counter,
                                              const std::vector<std::byte> &chassis_id)
    {
        ChassisEntry entry{
            .chassis_id = chassis_id_counter,
            .name_length = 0,
            .name = {},
        };
        if (chassis_id.size() > entry.name.size())
        {
            std::cerr << "Chassis ID has invalid size\n";
            return;
        }
        std::ranges::copy(chassis_id, entry.name.begin());
        entry.name_length = chassis_id.size();
        std::cout << "Chassis length: " << chassis_id.size();
        sendClientPacket(socket, ClientPacket(request_id, entry));
    }

    static inline void sendNeighbourEntry(uint16_t request_id,
                                          const OwnedFileDescriptor &socket,
                                          uint16_t chassis_id_counter,
                                          uint16_t neighbour_id_counter,
                                          const std::vector<std::byte> &port_id)
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
        std::ranges::copy(port_id, entry.neighbour_port.begin());
        sendClientPacket(socket, ClientPacket(request_id, entry));
    }

    void ClientSenderSocket::DumpData(uint16_t request_id)
    {
        uint16_t chassis_id_counter = 0;
        uint16_t neighbour_id_counter = 0;
        for (const auto &[chassis_id, port_map] : neighbour_list_->chassis_map)
        {
            sendChassisEntryPacket(request_id, socket_, chassis_id_counter, chassis_id);
            for (const auto &[port_id, neighbour] : port_map)
            {
                sendNeighbourEntry(request_id, socket_, chassis_id_counter, neighbour_id_counter, port_id);
                if (neighbour.ipv4_address.has_value())
                {
                    IpEntry entry{
                        .neighbour_id = neighbour_id_counter,
                        .address = neighbour.ipv4_address.value(),
                        .padding = {},
                    };
                    sendClientPacket(socket_, ClientPacket(request_id, entry));
                }
                if (neighbour.ipv6_address.has_value())
                {
                    Ipv6Entry entry{
                        .neighbour_id = neighbour_id_counter,
                        .address = neighbour.ipv6_address.value(),
                        .padding = {},
                    };
                    sendClientPacket(socket_, ClientPacket(request_id, entry));
                }
                neighbour_id_counter++;
            }
            chassis_id_counter++;
        }
        sendClientPacket(socket_, ClientPacket());
    }

    int ClientSenderSocket::GetSocket() const
    {
        return *socket_;
    }

    uint32_t ClientSenderSocket::GetEvents() const
    {
        return EPOLLIN;
    }

    std::expected<std::unique_ptr<ClientRepository>, int> ClientRepository::Create(EventManager &event_manager)
    {

        OwnedFileDescriptor socket = eventfd(0, 0);
        if (!socket.IsValid())
        {
            return std::unexpected(errno);
        }
        return std::make_unique<ClientRepository>(ClientRepository(std::move(socket), event_manager));
    }

    void ClientRepository::Call()
    {
        uint64_t value = 0;
        ssize_t result = read(*socket_, &value, sizeof(value));
        std::cout << "Read Data Transport Request " << result << "\n";
        if (result < 0)
        {
            std::cerr << "Failed to read ClientRepository eventfd\n";
        }
        std::cout << "Value " << value << "\n";
        for (long i = static_cast<long>(transport_sockets_.size()); i > 0; i--)
        {
            long idx = i - 1;

            if (transport_sockets_[idx]->EofReceived())
            {
                std::cout << "Eof received on socket, erasing...\n";
                transport_sockets_.erase(std::next(transport_sockets_.begin(), idx));
            }
        }
    }

    ClientListenSocket::ClientListenSocket(OwnedFileDescriptor &&lock_socket,
                                           OwnedFileDescriptor &&listener_socket,
                                           NeighbourList &neighbour_list,
                                           ClientRepository &dtr) : lock_socket_(std::move(lock_socket)),
                                                                    listener_socket_(std::move(listener_socket)),
                                                                    neighbour_list_(&neighbour_list),
                                                                    dtr_(&dtr)

    {
    }

    std::expected<void, int> ClientRepository::Add(std::shared_ptr<ClientSenderSocket> dts)
    {
        std::expected<size_t, int> add_result = event_manager_->Add(dts);
        if (!add_result.has_value())
        {
            return std::unexpected(add_result.error());
        }
        transport_sockets_.push_back(std::move(dts));
        return {};
    }

    std::expected<std::unique_ptr<ClientListenSocket>, int> ClientListenSocket::Create(
        NeighbourList &neighbour_list,
        ClientRepository &dtr)
    {
        ensureRuntimeDirectory();
        OwnedFileDescriptor lock_socket = open( // NOLINT(cppcoreguidelines-pro-type-vararg) No good alternative
            SOCKET_LOCK.c_str(),
            O_RDWR | O_CREAT,
            static_cast<mode_t>(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));
        if (!lock_socket.IsValid())
        {
            std::cerr << "Failed to obtain lockfile\n";
            return std::unexpected(errno);
        }
        if (flock(*lock_socket, LOCK_EX | LOCK_NB) == -1)
        {
            std::cerr << "Failed to obtain file lock\n";
            return std::unexpected(errno);
        }
        std::expected<OwnedFileDescriptor, int> socket = createDataSocket();
        if (!socket.has_value())
        {
            return std::unexpected(socket.error());
        }
        return std::make_unique<ClientListenSocket>(ClientListenSocket(std::move(lock_socket), std::move(socket.value()), neighbour_list, dtr));
    }

    void ClientListenSocket::Call()
    {
        std::cout << "New data connection...\n";
        sockaddr_un unix_addr{};
        socklen_t size = sizeof(unix_addr);
        OwnedFileDescriptor accept_socket = accept(*listener_socket_, reinterpret_cast<sockaddr *>(&unix_addr), &size);
        if (!accept_socket.IsValid())
        {
            std::cerr << "Data transport accept failed with errno: " << errno << "\n";
            return;
        }
        std::shared_ptr<ClientSenderSocket> dts = std::make_shared<ClientSenderSocket>(ClientSenderSocket(std::move(accept_socket), *neighbour_list_, dtr_->GetSocket()));
        std::expected<void, int> add_result = dtr_->Add(dts);
        if (!add_result.has_value())
        {
            std::cout << "Failed to add data transport socket, errno: " << add_result.error() << "\n";
        }
    }

    int ClientRepository::GetSocket() const
    {
        return *socket_;
    }
    uint32_t ClientRepository::GetEvents() const
    {
        return EPOLLIN;
    }

    std::expected<ClientReceiverSocket, int> ClientReceiverSocket::Create()
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

        std::ranges::copy(SOCKET_PATH, std::begin(address.sun_path));
        int bind_result = connect(*socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        if (bind_result < 0)
        {
            return std::unexpected(errno);
        }
        return ClientReceiverSocket(std::move(socket_fd));
    }

    constexpr int MAX_DATA_TRANSPORT_PACKET_AMOUNT = 100000;

    std::map<uint16_t, ClientReceiverSocket::DeviceData> ClientReceiverSocket::GetData()
    {
        std::map<uint16_t, std::vector<std::byte>> chassis_map{};
        std::map<uint16_t, ClientReceiverSocket::DeviceData> map{};
        sendRequest(socket_, request_id_);
        for (int i = 0; i < MAX_DATA_TRANSPORT_PACKET_AMOUNT; i++)
        {
            std::expected<std::variant<ChassisEntry, NeighbourEntry, IpEntry, Ipv6Entry, std::monostate>, int> packet = readDataPacket(*socket_);
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
                    .ipv4_address = std::nullopt,
                    .ipv6_address = std::nullopt,
                };
            }
            else if (IpEntry *ip_entry = std::get_if<IpEntry>(&*packet))
            {
                map[ip_entry->neighbour_id].ipv4_address = ip_entry->address;
            }
            else if (Ipv6Entry *ipv6_entry = std::get_if<Ipv6Entry>(&*packet))
            {
                map[ipv6_entry->neighbour_id].ipv6_address = ipv6_entry->address;
            }
            else
            {
                break;
            }
        }
        request_id_++;
        return map;
    }
} // namespace client
