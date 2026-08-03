
#include <bitset>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <memory>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <unistd.h>

#include "lldp.hh"
#include "netlink_monitor.hh"

using namespace std::chrono_literals;

namespace netlink
{

    constexpr unsigned int KERNEL_PID = 0;
    constexpr unsigned int RANDOM_SEQUENCE_MASK = 0x0FFF;
    constexpr std::chrono::milliseconds NETLINK_DELAY = 500ms;
    constexpr unsigned int NETLINK_DUMP_READ_ATTEMPTS = 5;

    static std::vector<TLVView> parseTlvs(std::span<std::byte> packet, size_t message_payload_size)
    {
        const long tlv_offset = RTA_ALIGN(NLMSG_LENGTH(message_payload_size));
        const nlmsghdr *const header = reinterpret_cast<nlmsghdr *>(packet.data());

        std::vector<TLVView> attributes;

        size_t remaining_bytes = NLMSG_PAYLOAD(header, message_payload_size);
        rtattr *start = reinterpret_cast<rtattr *>(std::next(packet.begin(), tlv_offset).base());
        for (rtattr *rta = start;
             RTA_OK(rta, remaining_bytes);
             rta = RTA_NEXT(rta, remaining_bytes))
        {
            TLVView tlv;
            std::memcpy(&tlv.attribute_header, rta, sizeof(rtattr));
            tlv.value = std::span<std::byte>(reinterpret_cast<std::byte *>(RTA_DATA(rta)), RTA_PAYLOAD(rta));
            attributes.emplace_back(tlv);
        }
        return attributes;
    }

    static LinkContentView parseLinkViewPacket(std::span<std::byte> packet)
    {
        nlmsghdr header{};
        std::memcpy(&header, packet.data(), sizeof(nlmsghdr));
        ifinfomsg interface_data{};
        std::memcpy(&interface_data, NLMSG_DATA(packet.data()), sizeof(ifinfomsg));

        std::vector<TLVView> attributes = parseTlvs(packet, sizeof(ifinfomsg));
        return LinkContentView{
            .interface_info = interface_data,
            .attributes = std::move(attributes),
        };
    }

    static AddrContentView parseAddrViewPacket(std::span<std::byte> packet)
    {
        // ifaddrmsg *address_info = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(packet.data()));
        nlmsghdr header{};
        std::memcpy(&header, packet.data(), sizeof(nlmsghdr));
        ifaddrmsg address_info{};
        std::memcpy(&address_info, NLMSG_DATA(packet.data()), sizeof(ifaddrmsg));
        std::vector<TLVView> attributes = parseTlvs(packet, sizeof(ifaddrmsg));
        return AddrContentView{
            .address_info = address_info,
            .attributes = std::move(attributes),
        };
    }

    static ErrorView parseErrorViewPacket(nlmsghdr &header, std::span<std::byte> packet)
    {
        nlmsgerr error_payload{};
        std::memcpy(&error_payload, NLMSG_DATA(packet.data()), sizeof(nlmsgerr));
        std::optional<MessageContentView>
            original_message = std::nullopt;
        size_t payload_size = sizeof(nlmsgerr);
        if ((header.nlmsg_flags & NLM_F_CAPPED) == 0)
        {
            std::byte *data = reinterpret_cast<std::byte *>(NLMSG_DATA(&(error_payload.msg)));
            size_t original_message_payload_size = NLMSG_PAYLOAD(&(error_payload.msg), 0);
            std::span<std::byte> original_message_span = std::span<std::byte>(data, original_message_payload_size);
            original_message = MessageContentView{
                .content = original_message_span,
            };
            payload_size = NLMSG_ALIGN(sizeof(nlmsgerr)) + original_message_payload_size;
        }
        std::vector<TLVView> attributes = parseTlvs(packet, payload_size);
        return ErrorView{
            .header = header,
            .content = ErrorContentView{
                .message_error = error_payload,
                .original_content = original_message,
                .attributes = std::move(attributes),
            },
        };
    }

    NetlinkPacketView packetViewParser(std::span<std::byte> packet)
    {
        nlmsghdr header{};
        std::memcpy(&header, packet.data(), sizeof(nlmsghdr));

        // nlmsghdr *header = reinterpret_cast<nlmsghdr *>(packet.data());
        if (header.nlmsg_type == RTM_GETLINK || header.nlmsg_type == RTM_NEWLINK || header.nlmsg_type == RTM_DELLINK)
        {
            return LinkView{
                .header = header,
                .content = parseLinkViewPacket(packet),
            };
        }

        if (header.nlmsg_type == RTM_GETADDR || header.nlmsg_type == RTM_NEWADDR || header.nlmsg_type == RTM_DELADDR)
        {
            return AddrView{
                .header = header,
                .content = parseAddrViewPacket(packet),
            };
        }

        if (header.nlmsg_type == NLMSG_DONE)
        {
            int error{};
            std::memcpy(&error, NLMSG_DATA(packet.data()), sizeof(int));
            return DoneView{
                .header = header,
                .content = error,
            };
        }

        if (header.nlmsg_type == NLMSG_ERROR)
        {
            return parseErrorViewPacket(header, packet);
        }
        size_t payload_size = NLMSG_PAYLOAD(&header, 0);
        std::byte *payload_start = reinterpret_cast<std::byte *>(NLMSG_DATA(packet.data()));
        std::span<std::byte> payload = std::span<std::byte>(payload_start, payload_size);

        return MessageView{
            .header = header,
            .content = MessageContentView{
                .content = payload,
            },
        };
    }

    const std::string &getMachineId()
    {
        static std::string machine_id;
        if (machine_id.empty())
        {
            std::ifstream machine_id_file_stream("/etc/machine-id");
            machine_id_file_stream >> machine_id;
            machine_id_file_stream.close();
        }
        return machine_id;
    }

    static inline void recvNetlinkMessageBatch(int socket_fd, std::vector<std::byte> &data_buffer)
    {
        if (socket_fd < 0)
        {
            data_buffer.clear();
            std::cerr << "Unable to load batch: socket not valid\n";
            return;
        }
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

        const long peek_data_length = recvmsg(socket_fd, &header_buffer, MSG_PEEK | MSG_TRUNC);
        if (peek_data_length <= 0)
        {
            data_buffer.clear();
            return;
        }
        if (static_cast<long>(data_buffer.size()) < peek_data_length)
        {
            data_buffer.resize(peek_data_length, std::byte{0x00});
        }

        buffer_data.iov_base = data_buffer.data();
        buffer_data.iov_len = peek_data_length;

        const long data_length = recvmsg(socket_fd, &header_buffer, 0);
        if (data_length != peek_data_length)
        {
            data_buffer.clear();
            return;
        }
        if (source_address.nl_pid != KERNEL_PID)
        {
            data_buffer.clear();
            return;
        }
        data_buffer.resize(data_length);
    }

    static inline std::optional<std::span<std::byte>> extractMessageFromBuffer(std::span<std::byte> &remaining_data)
    {
        size_t buffer_size = remaining_data.size();
        const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data.data());
        if (NLMSG_OK(header, buffer_size))
        {
            size_t next_message_size = NLMSG_ALIGN(header->nlmsg_len);
            const std::span<std::byte> message_span = remaining_data.subspan(0, next_message_size);
            remaining_data = remaining_data.subspan(next_message_size);
            return message_span;
        }
        return std::nullopt;
    }

    std::expected<std::unique_ptr<NetlinkSocket>, int> NetlinkSocket::Create(uint32_t multicast_groups)
    {
        int socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (socket_fd < 0)
        {
            std::cerr << "Failed to make socket\n";
            return std::unexpected(errno);
        }
        int enable = 1;
        setsockopt(socket_fd, SOL_NETLINK, NETLINK_EXT_ACK, static_cast<const void *>(&enable), sizeof(int));
        // unsigned int pid = getpid();
        sockaddr_nl address{};
        address.nl_family = AF_NETLINK;
        address.nl_pad = 0;
        address.nl_pid = 0;
        address.nl_groups = multicast_groups;
        int bind_result = bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(sockaddr_nl));
        if (bind_result < 0)
        {
            int err = errno;
            close(socket_fd);
            std::cerr << "Failed to bind: errno " << errno << "\n";
            return std::unexpected(err);
        }

        return std::make_unique<NetlinkSocket>(NetlinkSocket(socket_fd));
    }

    void NetlinkSocket::Call()
    {
        if (!callback_.has_value())
        {
            std::cerr << "NetlinkSocket lacks callback\n";
            return;
        }
        std::vector<std::byte> data_buffer;
        recvNetlinkMessageBatch(*socket_fd_, data_buffer);
        std::span<std::byte> remaining_data = std::span<std::byte>(data_buffer.begin(), data_buffer.end());
        for (std::optional<std::span<std::byte>> packet_from_buffer = extractMessageFromBuffer(remaining_data);
             packet_from_buffer.has_value();
             packet_from_buffer = extractMessageFromBuffer(remaining_data))
        {
            (*callback_)(packetViewParser(packet_from_buffer.value())); // NOLINT(bugprone-unchecked-optional-access)
        }
    }

    std::expected<int, int> NetlinkSocket::SendGetLinkDumpMessage()
    {
        if (!socket_fd_.IsValid())
        {
            return std::unexpected(-2);
        }
        sequence_number_++;
        alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifinfomsg))> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence_number_;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_DUMP | NLM_F_REQUEST;
        netlink_header->nlmsg_type = RTM_GETLINK;
        ifinfomsg *interface_info = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(netlink_header));
        interface_info->ifi_family = AF_UNSPEC;
        interface_info->ifi_type = 0;
        interface_info->ifi_change = 0;
        interface_info->ifi_flags = 0;
        interface_info->ifi_index = 0;

        const long bytes_sent = send(*socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
        if (bytes_sent < netlink_header->nlmsg_len)
        {
            return std::unexpected(-3);
        }
        return sequence_number_;
    }

    std::expected<int, int> NetlinkSocket::SendGetAddrMessage()
    {
        if (!socket_fd_.IsValid())
        {
            return std::unexpected(-2);
        }
        sequence_number_++;
        alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifaddrmsg))> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence_number_;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_REQUEST | NLM_F_DUMP;
        netlink_header->nlmsg_type = RTM_GETADDR;
        ifaddrmsg *address_message = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(netlink_header));
        address_message->ifa_family = AF_UNSPEC;
        address_message->ifa_prefixlen = 0;
        address_message->ifa_scope = RT_SCOPE_UNIVERSE;
        address_message->ifa_flags = 0;
        address_message->ifa_index = 0;

        const long bytes_sent = send(*socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
        if (bytes_sent < netlink_header->nlmsg_len)
        {
            return std::unexpected(-3);
        }
        return sequence_number_;
    }

} // namespace netlink
