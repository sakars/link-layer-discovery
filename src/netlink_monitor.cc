
#include "netlink_monitor.hh"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if_arp.h>
#include <iostream>
#include <unistd.h>
#include <memory>
#include <net/if.h>
#include <bitset>
#include <functional>

using namespace std::chrono_literals;

namespace ndisc
{
    NetlinkSocketReader::NetlinkSocketReader(int socket_fd) : socket_fd_(socket_fd)
    {
    }

    constexpr unsigned int KERNEL_PID = 0;
    constexpr unsigned int RANDOM_SEQUENCE_MASK = 0x0FFF;
    constexpr std::chrono::milliseconds NETLINK_DELAY = 500ms;
    constexpr unsigned int NETLINK_DUMP_READ_ATTEMPTS = 5;

    static inline void loadBatch(int socket_fd, std::vector<uint8_t> &data_buffer)
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

        data_buffer = std::vector<uint8_t>(peek_data_length, 0);

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

    static inline std::optional<std::span<uint8_t>> tryLoadFromSpan(std::span<uint8_t> &remaining_data)
    {
        size_t buffer_size = remaining_data.size();
        const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data.data());
        if (NLMSG_OK(header, buffer_size))
        {
            size_t next_message_size = NLMSG_ALIGN(header->nlmsg_len);
            const std::span<uint8_t> message_span = remaining_data.subspan(0, next_message_size);
            remaining_data = remaining_data.subspan(next_message_size);
            return message_span;
        }
        return std::nullopt;
    }

    std::optional<std::span<uint8_t>> NetlinkSocketReader::Next()
    {
        std::optional<std::span<uint8_t>> packet_from_remaining = tryLoadFromSpan(remaining_data_);
        if (packet_from_remaining.has_value())
        {
            return packet_from_remaining;
        }
        loadBatch(socket_fd_, data_buffer_);
        remaining_data_ = std::span<uint8_t>(data_buffer_.begin(), data_buffer_.end());
        return tryLoadFromSpan(remaining_data_);
    }

    NetlinkPacketReader::NetlinkPacketReader(int socket_fd) : socket_fd_(socket_fd), socket_reader_(socket_fd)
    {
    }

    std::unique_ptr<NetlinkPacketReader> NetlinkPacketReader::Create(unsigned int subscribed_groups)
    {
        int socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (socket_fd < 0)
        {
            std::cerr << "Failed to create socket...\n";
            return nullptr;
        }
        int enable = 1;
        int ext_ack_result = setsockopt(socket_fd, SOL_NETLINK, NETLINK_EXT_ACK, &enable, sizeof(enable));
        if (ext_ack_result != 0)
        {
            std::cerr << "Netlink socket option NETLINK_EXT_ACK set failed: " << errno << '\n';
        }
        int cap_ack_result = setsockopt(socket_fd, SOL_NETLINK, NETLINK_CAP_ACK, &enable, sizeof(enable));
        if (cap_ack_result != 0)
        {
            std::cerr << "Netlink socket option NETLINK_CAP_ACK set failed: " << errno << '\n';
        }

        std::unique_ptr<sockaddr_nl> address = std::make_unique<sockaddr_nl>(sockaddr_nl{
            .nl_family = AF_NETLINK,
            .nl_pad = 0,
            .nl_pid = static_cast<unsigned int>(getpid()),
            .nl_groups = subscribed_groups, // RTMGRP_LINK | RTMGRP_IPV4_IFADDR,
        });
        int bind_result = bind(socket_fd, reinterpret_cast<sockaddr *>(address.get()), sizeof(sockaddr_nl));
        if (bind_result != 0)
        {
            std::cerr << "Netlink socket bind error: " << errno << '\n';
        }
        return std::make_unique<NetlinkPacketReader>(NetlinkPacketReader(socket_fd));
    }

    void NetlinkPacketReader::Call()
    {
        if (packet_handler.has_value())
        {
            packet_handler.value()(*this);
        }
    }

    void NetlinkPacketReader::Register(int epfd)
    {
        epoll_event events{};
        events.events = EPOLLIN;
        events.data.ptr = this;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd_, &events) != 0)
        {
            std::cerr << "Failed to add socket to epoll\n";
        }
    }

    void NetlinkPacketReader::Deregister(int epfd)
    {
        epoll_event events{};
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, socket_fd_, &events) != 0)
        {
            std::cerr << "Failed to delete epoll register\n";
        }
    }

    std::optional<NetlinkPacketView> NetlinkPacketReader::Next()
    {
        std::optional<std::span<uint8_t>> packet_content = socket_reader_.Next();
        if (!packet_content.has_value())
        {
            return std::nullopt;
        }
        std::span<uint8_t> packet = packet_content.value();
        nlmsghdr *header = reinterpret_cast<nlmsghdr *>(packet.data());
        if (header->nlmsg_type == RTM_GETLINK || header->nlmsg_type == RTM_NEWLINK || header->nlmsg_type == RTM_DELLINK)
        {
            ifinfomsg *interface_data = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(header));
            size_t remaining_bytes = IFLA_PAYLOAD(header);
            std::vector<TLVView> attributes;
            for (rtattr *rta = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(ifinfomsg))));
                 RTA_OK(rta, remaining_bytes);
                 rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), rta->rta_len),
                    });
            }
            return LinkView{
                .header = header,
                .content = LinkContentView{
                    .interface_info = interface_data,
                    .attributes = std::move(attributes),
                },
            };
        }

        if (header->nlmsg_type == RTM_GETADDR || header->nlmsg_type == RTM_NEWADDR || header->nlmsg_type == RTM_DELADDR)
        {
            ifaddrmsg *address_info = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(header));
            size_t remaining_bytes = IFA_PAYLOAD(header);
            std::vector<TLVView> attributes;
            for (rtattr *rta = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(ifaddrmsg))));
                 RTA_OK(rta, remaining_bytes);
                 rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), rta->rta_len),
                    });
            }
            return AddrView{
                .header = header,
                .content = AddrContentView{
                    .address_info = address_info,
                    .attributes = std::move(attributes),
                },
            };
        }

        if (header->nlmsg_type == NLMSG_DONE)
        {
            int *error = reinterpret_cast<int *>(NLMSG_DATA(header));
            return DoneView{
                .header = header,
                .error = error,
            };
        }

        if (header->nlmsg_type == NLMSG_ERROR)
        {
            nlmsgerr *error_payload = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(header));
            size_t remaining_bytes = NLMSG_PAYLOAD(header, sizeof(nlmsgerr));
            std::optional<MessageContentView> original_message = std::nullopt;
            rtattr *attribute_base = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(nlmsgerr))));
            if ((header->nlmsg_flags & NLM_F_CAPPED) == 0)
            {
                uint8_t *data = reinterpret_cast<uint8_t *>(NLMSG_DATA(&(error_payload->msg)));
                size_t original_message_payload_size = NLMSG_PAYLOAD(&(error_payload->msg), 0);
                std::span<uint8_t> original_message_span = std::span<uint8_t>(data, original_message_payload_size);
                original_message = MessageContentView{
                    .content = original_message_span,
                };
                remaining_bytes = NLMSG_PAYLOAD(header, NLMSG_ALIGN(sizeof(nlmsgerr)) + original_message_payload_size);
                attribute_base = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(nlmsgerr)) + original_message_payload_size));
            }
            std::vector<TLVView> attributes;
            for (rtattr *rta = attribute_base; RTA_OK(rta, remaining_bytes); rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), rta->rta_len),
                    });
            }
            return ErrorView{
                .header = header,
                .error = error_payload,
                .original_content = original_message,
                .attributes = attributes,
            };
        }
        size_t payload_size = NLMSG_PAYLOAD(header, 0);
        uint8_t *payload_start = reinterpret_cast<uint8_t *>(NLMSG_DATA(header));
        std::span<uint8_t> payload = std::span<uint8_t>(payload_start, payload_size);

        // std::cerr << "Unrecognized packet...\n";
        return MessageView{
            .header = header,
            .content = MessageContentView{
                .content = payload,
            },
        };
    }

} // namespace ndisc
