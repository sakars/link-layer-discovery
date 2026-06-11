

#include "netlink.hh"
#include <array>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <memory.h>
#include <net/if_arp.h>
#include <optional>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <variant>
#include <vector>
#include <iostream>
#include <iomanip>

namespace ndisc
{
    constexpr unsigned int MAX_MESSAGE_BUFFER_SIZE = 4096;
    constexpr unsigned int MAX_GET_LINK_DUMP_MAX_READS = 32;
    constexpr unsigned int MAX_GET_ADDR_DUMP_MAX_READS = 32;

    static inline long sendGetLinkDumpMessage(int socket_fd, decltype(nlmsghdr::nlmsg_seq) sequence)
    {
        if (socket_fd < 0)
        {
            return -2;
        }
        alignas(nlmsghdr) std::array<uint8_t, MAX_MESSAGE_BUFFER_SIZE> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_DUMP | NLM_F_REQUEST;
        netlink_header->nlmsg_type = RTM_GETLINK;
        ifinfomsg *interface_info = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(netlink_header));
        interface_info->ifi_family = AF_UNSPEC;
        interface_info->ifi_type = 0;
        interface_info->ifi_change = 0;
        interface_info->ifi_flags = 0;
        interface_info->ifi_index = 0;

        const long BYTES_SENT = send(socket_fd, buffer.data(), netlink_header->nlmsg_len, 0);
        if (BYTES_SENT < netlink_header->nlmsg_len)
        {
            return -3;
        }
        return BYTES_SENT;
    }

    static inline std::vector<uint8_t> recvMessages(int socket_fd)
    {
        sockaddr_nl source_address{};
        uint8_t tmp_data = 0;
        iovec buffer_data{
            .iov_base = &tmp_data,
            .iov_len = 1,
        };
        msghdr header_buffer{
            .msg_name = &source_address,
            .msg_namelen = sizeof(source_address),
            .msg_iov = &buffer_data,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };

        const long PEEK_DATA_LENGTH = recvmsg(socket_fd, &header_buffer, MSG_PEEK | MSG_TRUNC);
        if (PEEK_DATA_LENGTH <= 0)
        {
            return {};
        }

        std::vector<uint8_t> data_buffer = std::vector<uint8_t>(PEEK_DATA_LENGTH, 0);

        buffer_data.iov_base = data_buffer.data();
        buffer_data.iov_len = PEEK_DATA_LENGTH;

        const long DATA_LENGTH = recvmsg(socket_fd, &header_buffer, 0);
        if (DATA_LENGTH != PEEK_DATA_LENGTH)
        {
            return {};
        }
        constexpr unsigned int KERNEL_PID = 0;
        if (source_address.nl_pid != KERNEL_PID)
        {
            return {};
        }
        data_buffer.resize(DATA_LENGTH);
        return data_buffer;
    }

    static inline void reportErrorResponse(const nlmsghdr *data_header)
    {
        std::cerr << "Kernel returned error: ";
        const nlmsgerr *error = reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(data_header));
        std::cerr << "Error code " << error->error;
        size_t error_size = sizeof(nlmsgerr);
        if ((data_header->nlmsg_flags & NLM_F_CAPPED) == 0)
        {
            const nlmsghdr *error_header = &error->msg;
            error_size += NLMSG_PAYLOAD(error_header, 0);
        }
        size_t rta_remaining_size = NLMSG_PAYLOAD(data_header, error_size);
        for (const rtattr *attribute = reinterpret_cast<const rtattr *>(reinterpret_cast<const uint8_t *>(data_header) + RTA_ALIGN(NLMSG_LENGTH(error_size)));
             RTA_OK(attribute, rta_remaining_size);
             attribute = RTA_NEXT(attribute, rta_remaining_size))
        {
            if (attribute->rta_type == NLMSGERR_ATTR_MSG)
            {
                size_t payload_size = RTA_PAYLOAD(attribute);
                if (payload_size > 0)
                {
                    payload_size--;
                }
                std::string_view message = std::string_view(reinterpret_cast<const char *>(RTA_DATA(attribute)), payload_size);
                std::cerr << "Message: \n"
                          << message;
            }
        }
    }

    static inline std::optional<InterfaceDumpEntry> extractInterfaceDataFromMessage(const nlmsghdr *data_header)
    {
        const ifinfomsg *interface_data = reinterpret_cast<const ifinfomsg *>(NLMSG_DATA(data_header));
        if (interface_data->ifi_type != ARPHRD_ETHER)
        {
            return std::nullopt;
        }
        InterfaceDumpEntry interface_entry{};
        interface_entry.interface_index = interface_data->ifi_index;

        int rta_remaining_size = IFLA_PAYLOAD(data_header);

        for (rtattr *attribute = IFLA_RTA(interface_data); RTA_OK(attribute, rta_remaining_size); attribute = RTA_NEXT(attribute, rta_remaining_size))
        {
            if (attribute->rta_type == IFLA_ADDRESS)
            {
                size_t address_size = RTA_PAYLOAD(attribute);
                if (address_size == 6)
                {
                    interface_entry.mac_address = std::array<uint8_t, 6>();
                    memcpy(interface_entry.mac_address.value().data(), RTA_DATA(attribute), address_size);
                }
            }
            else if (attribute->rta_type == IFLA_IFNAME)
            {
                size_t name_size = RTA_PAYLOAD(attribute);
                interface_entry.interface_name = std::string_view(reinterpret_cast<const char *>(RTA_DATA(attribute)), name_size);
            }
        }
        return interface_entry;
    }

    static inline bool processLinkDumpMessage(std::vector<InterfaceDumpEntry> &interfaces, const nlmsghdr *data_header, decltype(nlmsghdr::nlmsg_seq) request_sequence, int pid)
    {
        if (data_header->nlmsg_seq != request_sequence ||            // response not relevant to request
            data_header->nlmsg_pid != static_cast<unsigned int>(pid) // destination process mismatch
        )
        {
            return false;
        }

        if (data_header->nlmsg_flags & NLM_F_DUMP_INTR)
        {
            return true;
        }
        if (data_header->nlmsg_type == NLMSG_ERROR)
        {
            reportErrorResponse(data_header);
        }
        else if (data_header->nlmsg_type == NLMSG_DONE)
        {

            int *done_error = reinterpret_cast<int *>(NLMSG_DATA(data_header));
            if (*done_error != 0)
            {
                std::cerr << "Dump finished with error...\n";
            }
            return true;
        }
        else if (data_header->nlmsg_type == RTM_NEWLINK)
        {
            const std::optional<InterfaceDumpEntry> message = extractInterfaceDataFromMessage(data_header);
            if (message.has_value())
            {
                interfaces.emplace_back(std::move(message.value()));
            }
        }
        return false;
    }

    static inline std::vector<InterfaceDumpEntry> receiveGetLinkDumpContents(int socket_fd, decltype(nlmsghdr::nlmsg_seq) request_sequence, int pid)
    {

        bool done = false;
        std::vector<InterfaceDumpEntry> interfaces{};
        for (unsigned int timeout = 0; !done && timeout < MAX_GET_LINK_DUMP_MAX_READS; timeout++)
        {
            const std::vector<uint8_t> DATA_BUFFER = recvMessages(socket_fd);
            unsigned int remaining_size = DATA_BUFFER.size();

            bool dump_interrupted = false;

            for (
                const nlmsghdr *data_header = reinterpret_cast<const nlmsghdr *>(DATA_BUFFER.data());
                NLMSG_OK(data_header, remaining_size);
                data_header = NLMSG_NEXT(data_header, remaining_size))
            {
                if (processLinkDumpMessage(interfaces, data_header, request_sequence, pid))
                {
                    done = true;
                }
            }
            if (dump_interrupted)
            {
                std::cerr << "Warning: Dump was interrupted";
            }
        }
        return interfaces;
    }

    static inline long sendGetAddrMessage(int socket_fd, decltype(nlmsghdr::nlmsg_seq) sequence, unsigned int interface_index)
    {
        if (socket_fd < 0)
        {
            return -2;
        }
        alignas(nlmsghdr) std::array<uint8_t, MAX_MESSAGE_BUFFER_SIZE> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_REQUEST | NLM_F_DUMP;
        netlink_header->nlmsg_type = RTM_GETADDR;
        ifaddrmsg *address_message = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(netlink_header));
        address_message->ifa_family = AF_INET;
        address_message->ifa_prefixlen = 0;
        address_message->ifa_scope = IFA_UNSPEC;
        address_message->ifa_flags = 0;
        address_message->ifa_index = interface_index;

        const long BYTES_SENT = send(socket_fd, buffer.data(), netlink_header->nlmsg_len, 0);
        if (BYTES_SENT < netlink_header->nlmsg_len)
        {
            return -3;
        }
        return BYTES_SENT;
    }

    static inline void parseGetAddrMessage(int socket_fd, decltype(nlmsghdr::nlmsg_seq) sequence, unsigned int interface_index)
    {
        for (unsigned int timeout = 0; timeout < MAX_GET_ADDR_DUMP_MAX_READS; timeout++)
        {
            const std::vector<uint8_t> DATA_BUFFER = recvMessages(socket_fd);
            unsigned int remaining_size = DATA_BUFFER.size();
            for (
                const nlmsghdr *data_header = reinterpret_cast<const nlmsghdr *>(DATA_BUFFER.data());
                NLMSG_OK(data_header, remaining_size);
                data_header = NLMSG_NEXT(data_header, remaining_size))
            {
                if (data_header->nlmsg_flags & NLM_F_DUMP_INTR)
                {
                    std::cerr << "Dump interrupt received";
                }
                if (data_header->nlmsg_type == NLMSG_ERROR)
                {
                    reportErrorResponse(data_header);
                }
                else if (data_header->nlmsg_type == RTM_NEWADDR)
                {
                    const ifaddrmsg *address_message = reinterpret_cast<const ifaddrmsg *>(NLMSG_DATA(data_header));
                }
            }
        }
    }

    NetlinkSocket::NetlinkSocket() : socket_fd_(socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE))
    {
        if (socket_fd_ < 0)
        {
            return;
        }
        int enable = 1;
        setsockopt(socket_fd_, SOL_NETLINK, NETLINK_EXT_ACK, static_cast<const void *>(&enable), sizeof(int));
        // setsockopt(socket_fd_, SOL_NETLINK, NETLINK_CAP_ACK, static_cast<const void *>(&enable), sizeof(int));
        unsigned int pid = getpid();
        address_ = std::make_unique<sockaddr_nl>(sockaddr_nl{
            .nl_family = AF_NETLINK,
            .nl_pad = 0,
            .nl_pid = pid,
            .nl_groups = 0,
        });
        int bind_result = bind(socket_fd_, (const sockaddr *)(address_.get()), sizeof(sockaddr_nl));
        if (bind_result < 0)
        {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        sequence_number_ = rand() & 0xFFFF;
    }

    NetlinkSocket::~NetlinkSocket()
    {
        if (socket_fd_ >= 0)
        {
            close(socket_fd_);
        }
    }

    NetlinkSocket::NetlinkSocket(NetlinkSocket &&other) noexcept : socket_fd_(other.socket_fd_), address_(std::move(other.address_))
    {
        other.socket_fd_ = -1;
    }

    NetlinkSocket &NetlinkSocket::operator=(NetlinkSocket &&other) noexcept
    {
        if (socket_fd_ >= 0)
        {
            close(socket_fd_);
            address_ = nullptr;
        }
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
        address_ = std::move(other.address_);
        return *this;
    }

    std::variant<std::array<uint8_t, 4>, NetlinkSocket::NetlinkError, std::monostate> NetlinkSocket::GetIpAddressOfDevice(const std::string & /*unused*/)
    {
        // if (socket_fd_ < 0)
        // {
        //     return NetlinkSocket::NetlinkError::SOCKET_CLOSED;
        // }

        // nlmsghdr dump_header{
        //     .nlmsg_len = sizeof(nlmsghdr),
        //     .nlmsg_type = RTM_GETADDR,
        //     .nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
        //     .nlmsg_seq = sequence_number_,
        //     .nlmsg_pid = 0,
        // };
        // sockaddr_nl destination_address{
        //     .nl_family = AF_NETLINK,
        //     .nl_pad = 0,
        //     .nl_groups = 0,
        //     .nl_pid = 0,
        // };
        // msghdr message{
        //     .msg_name = &destination_address,
        //     .msg_namelen = sizeof(sockaddr_nl),
        //     .msg_iov = nullptr,
        //     .msg_iovlen = 0,
        //     .msg_control = nullptr,
        //     .msg_controllen = 0,
        //     .msg_flags = 0,
        // };

        // sequence_number_++;
        return std::monostate();
    }

    bool NetlinkSocket::IsSocketOk() const
    {
        return socket_fd_ >= 0;
    }

    std::vector<InterfaceDumpEntry> NetlinkSocket::GetDevices()
    {
        const unsigned int REQUEST_SEQUENCE_NUMBER = sequence_number_++;
        const long status = sendGetLinkDumpMessage(socket_fd_, REQUEST_SEQUENCE_NUMBER);
        if (status < 0)
        {
            std::cerr << "Invalid status: " << status << "\n";
            return {};
        }
        std::vector<InterfaceDumpEntry> interfaces = receiveGetLinkDumpContents(socket_fd_, REQUEST_SEQUENCE_NUMBER, static_cast<int>(address_->nl_pid));
        return interfaces;
    }

} // namespace ndisc