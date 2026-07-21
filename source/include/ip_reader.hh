#ifndef IP_READER_HH
#define IP_READER_HH

#include <memory>
#include <optional>

#include "netlink_monitor.hh"

namespace netlink
{
    class IpReader
    {
        std::shared_ptr<NetlinkSocket> reader_socket_;
        std::optional<unsigned int> dump_request_sequence_number_;
        int dump_read_timeout_{-1};

        std::optional<std::function<void(AddrView)>> address_update_callback_;
        std::optional<std::function<void()>> end_of_ip_dump_callback_;
        std::optional<std::function<void()>> dump_errored_callback_;

        void HandleNetlinkPacket(NetlinkPacketView);
        bool BindReaderSocketCallback();
        void ResetDumpAttempt();

        IpReader(std::shared_ptr<NetlinkSocket> reader) : reader_socket_(std::move(reader))
        {
            BindReaderSocketCallback();
        }

    public:
        IpReader(const IpReader &) = delete;
        IpReader(IpReader &&other) noexcept : reader_socket_(std::move(other.reader_socket_)),
                                              dump_request_sequence_number_(other.dump_request_sequence_number_),
                                              dump_read_timeout_(other.dump_read_timeout_),
                                              address_update_callback_(std::move(other.address_update_callback_)),
                                              end_of_ip_dump_callback_(std::move(other.end_of_ip_dump_callback_)),
                                              dump_errored_callback_(std::move(other.dump_errored_callback_))
        {
            other.reader_socket_.reset();
            other.dump_request_sequence_number_.reset();
            other.dump_read_timeout_ = -1;
            other.address_update_callback_.reset();
            other.end_of_ip_dump_callback_.reset();
            other.dump_errored_callback_.reset();
            BindReaderSocketCallback();
        }
        IpReader &operator=(const IpReader &) = delete;
        IpReader &operator=(IpReader &&other)
        {
            reader_socket_ = std::move(other.reader_socket_);
            dump_request_sequence_number_ = other.dump_request_sequence_number_;
            dump_read_timeout_ = other.dump_read_timeout_;
            address_update_callback_ = std::move(other.address_update_callback_);
            end_of_ip_dump_callback_ = std::move(other.end_of_ip_dump_callback_);
            dump_errored_callback_ = std::move(other.dump_errored_callback_);
            other.reader_socket_.reset();
            other.dump_request_sequence_number_.reset();
            other.dump_read_timeout_ = -1;
            other.address_update_callback_.reset();
            other.end_of_ip_dump_callback_.reset();
            other.dump_errored_callback_.reset();
            return *this;
        };
        ~IpReader()
        {
            if (reader_socket_ != nullptr)
            {
                reader_socket_->ClearCallback();
            }
        }

        static std::expected<std::unique_ptr<IpReader>, int> Create(ndisc::EventManager &manager);

        void Tick(const uint64_t&);

        std::expected<void, int> TriggerDump();

        void SetAddressUpdateCallback(std::function<void(AddrView)> callback)
        {
            address_update_callback_ = std::move(callback);
        }

        void ClearAddressUpdateCallback() { address_update_callback_ = std::nullopt; }

        void SetEndOfIpDumpCallback(std::function<void()> callback)
        {
            end_of_ip_dump_callback_ = std::move(callback);
        }

        void ClearEndOfIpDumpCallback() { end_of_ip_dump_callback_ = std::nullopt; }

        void SetDumpErroredCallback(std::function<void()> callback)
        {
            dump_errored_callback_ = std::move(callback);
        }

        void ClearDumpErroredCallback() { dump_errored_callback_ = std::nullopt; }
    };

} // namespace netlink

#endif // IP_READER_HH