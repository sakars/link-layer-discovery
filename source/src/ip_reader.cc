#include "ip_reader.hh"

#include <iostream>

namespace netlink
{

    static constexpr int DUMP_READ_TIMEOUT = 5;

    std::expected<std::unique_ptr<IpReader>, int> IpReader::Create(ndisc::EventManager &manager)
    {
        std::expected<std::unique_ptr<NetlinkSocket>, int> netlink_socket_result = NetlinkSocket::Create(0);
        if (!netlink_socket_result.has_value() || *netlink_socket_result == nullptr)
        {
            return std::unexpected(netlink_socket_result.error_or(0));
        }
        std::shared_ptr<NetlinkSocket> netlink_socket = std::move(*netlink_socket_result);
        std::expected<size_t, int> add_result = manager.Add(netlink_socket);
        if (!add_result.has_value())
        {
            return std::unexpected(add_result.error());
        }

        return std::make_unique<IpReader>(std::move(IpReader(std::move(netlink_socket))));
    }

    void IpReader::Tick(const uint64_t &delta_seconds)
    {
        if (dump_read_timeout_ > delta_seconds)
        {
            dump_read_timeout_ -= delta_seconds; // NOLINT(bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions)
        }
        else
        {
            dump_read_timeout_ = 0;
            ResetDumpAttempt();
            if (dump_errored_callback_.has_value())
            {
                (*dump_errored_callback_)();
            }
        }
    }

    bool IpReader::BindReaderSocketCallback()
    {
        if (reader_socket_ != nullptr)
        {
            reader_socket_->SetCallback([this](NetlinkPacketView packet)
                                        { this->HandleNetlinkPacket(std::move(packet)); });
        }
        return reader_socket_ != nullptr;
    }

    void IpReader::ResetDumpAttempt()
    {
        dump_read_timeout_ = -1;
        dump_request_sequence_number_ = std::nullopt;
    }

    std::expected<void, int> IpReader::TriggerDump()
    {
        if (reader_socket_ == nullptr)
        {
            std::cerr << "Failed to issue dump, socket empty\n";
            return std::unexpected(EINVAL);
        }
        dump_read_timeout_ = DUMP_READ_TIMEOUT;
        std::expected<int, int> dump_result = reader_socket_->SendGetAddrMessage();
        if (!dump_result.has_value())
        {
            return std::unexpected(dump_result.error());
        }
        dump_request_sequence_number_ = *dump_result;
        return {};
    }

    void IpReader::HandleNetlinkPacket(NetlinkPacketView packet)
    {
        if (!dump_request_sequence_number_.has_value())
        {
            std::cerr << "Ip reader received unexpected packet, ignoring...\n";
            return;
        }
        unsigned int packet_sequence_number = std::visit([&]<typename T>(NetlinkMessage<T> packet)
                                                         { return packet.header.nlmsg_seq; }, packet);
        if (packet_sequence_number != dump_request_sequence_number_.value())
        {
            std::cerr << "Ip reader received out of sequence packet, ignoring...\n";
            return;
        }
        if (AddrView *address_update_message = std::get_if<AddrView>(&packet))
        {
            if (address_update_callback_.has_value())
            {
                (*address_update_callback_)(*address_update_message);
            }
            else
            {
                std::cout << "Address message got skipped as no callback is assigned.\n";
            }
        }
        else if (std::get_if<DoneView>(&packet) != nullptr)
        {
            ResetDumpAttempt();
            if (end_of_ip_dump_callback_.has_value())
            {
                (*end_of_ip_dump_callback_)();
            }
        }
        else if (std::get_if<ErrorView>(&packet) != nullptr)
        {
            ResetDumpAttempt();
            if (dump_errored_callback_.has_value())
            {
                (*dump_errored_callback_)();
            }
        }
    }
} // namespace netlink