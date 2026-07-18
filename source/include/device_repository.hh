#ifndef DEVICE_REPOSITORY_HH
#define DEVICE_REPOSITORY_HH

#include <chrono>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "netlink_monitor.hh"

using namespace std::chrono_literals;

namespace ndisc
{

    enum class ReaderState : uint8_t
    {
        IDLE,
        READING,
        ERRORED,
    };

    class DeviceReader : std::enable_shared_from_this<DeviceReader>
    {
    public:
        using Callback = std::function<void(LinkView)>;

    private:
        std::shared_ptr<NetlinkSocket> reader_socket_;
        std::optional<unsigned int> request_sequence_number_;
        ReaderState reader_state_ = ReaderState::IDLE;
        std::optional<Callback> callback_;
        int dump_timer_ = 0;

        void ReceivePacket(NetlinkPacketView packet);

        void BindCallback();

        DeviceReader(std::shared_ptr<NetlinkSocket> socket) : reader_socket_(std::move(socket))
        {
            BindCallback();
        }

    public:
        DeviceReader(DeviceReader &&other) noexcept : reader_socket_(std::move(other.reader_socket_)),
                                                      request_sequence_number_(other.request_sequence_number_),
                                                      reader_state_(other.reader_state_),
                                                      callback_(std::move(other.callback_)),
                                                      dump_timer_(other.dump_timer_)
        {
            other.reader_socket_.reset();
            other.request_sequence_number_.reset();
            other.reader_state_ = ReaderState::ERRORED;
            other.callback_.reset();
            other.dump_timer_ = INT_MAX;
            BindCallback();
        }

        DeviceReader(const DeviceReader &) = delete;
        DeviceReader &operator=(DeviceReader &&other) noexcept
        {
            reader_socket_->SetCallback([](const NetlinkPacketView &) {});

            reader_socket_ = std::move(other.reader_socket_);
            request_sequence_number_ = other.request_sequence_number_;
            reader_state_ = other.reader_state_;
            callback_ = std::move(other.callback_);
            dump_timer_ = other.dump_timer_;
            other.reader_socket_.reset();
            other.request_sequence_number_.reset();
            other.reader_state_ = ReaderState::ERRORED;
            other.callback_.reset();
            other.dump_timer_ = INT_MAX;
            return *this;
        }
        DeviceReader &operator=(const DeviceReader &) = delete;
        ~DeviceReader()
        {
            reader_socket_->SetCallback([](const NetlinkPacketView &) {});
        }

        static std::expected<std::shared_ptr<DeviceReader>, int> Create(EventManager &);

        void Tick();

        bool IsReaderIdle()
        {
            return reader_state_ == ReaderState::IDLE;
        }

        void ExpediteLinkDump();

        void SetCallback(Callback callback)
        {
            callback_ = std::move(callback);
        }
    };

    class IpReader : std::enable_shared_from_this<IpReader>
    {
    public:
        using Callback = std::function<void(AddrView)>;

    private:
        ReaderState reader_state_ = ReaderState::IDLE;
        std::optional<unsigned int> sequence_number_;
        int dump_timer_ = 0;
        std::shared_ptr<NetlinkSocket> reader_socket_;
        std::optional<Callback> callback_;

        void ReceivePacket(NetlinkPacketView);

        void BindCallback();

        IpReader(std::shared_ptr<NetlinkSocket> socket) : reader_socket_(std::move(socket))
        {
            BindCallback();
        }

    public:
        IpReader(IpReader &&other) noexcept : reader_state_(other.reader_state_),
                                              sequence_number_(other.sequence_number_),
                                              dump_timer_(other.dump_timer_),
                                              reader_socket_(other.reader_socket_),
                                              callback_(other.callback_)
        {
            other.reader_state_ = ReaderState::ERRORED;
            other.sequence_number_ = std::nullopt;
            other.dump_timer_ = INT_MAX;
            other.reader_socket_.reset();
            other.callback_ = std::nullopt;
            BindCallback();
        }
        IpReader(const IpReader &) = delete;
        IpReader &operator=(IpReader &&other) noexcept
        {
            reader_state_ = other.reader_state_;
            sequence_number_ = other.sequence_number_;
            dump_timer_ = other.dump_timer_;
            reader_socket_ = other.reader_socket_;
            callback_ = std::nullopt;
            other.reader_state_ = ReaderState::ERRORED;
            other.sequence_number_ = std::nullopt;
            other.dump_timer_ = INT_MAX;
            other.reader_socket_.reset();
            other.callback_ = std::nullopt;
            BindCallback();
            return *this;
        }
        IpReader &operator=(const IpReader &) = delete;
        ~IpReader()
        {
            reader_socket_->SetCallback([](const NetlinkPacketView &) {});
        }

        static std::expected<std::shared_ptr<IpReader>, int> Create(EventManager &);

        void Tick();

        bool IsReaderIdle()
        {
            return reader_state_ == ReaderState::IDLE;
        }

        void ExpediteAddrDump();

        void SetCallback(Callback callback)
        {
            callback_ = std::move(callback);
        }
    };

    // struct IpReader
    // {
    //     ReaderState reader_state = ReaderState::IDLE;
    //     std::optional<unsigned int> sequence_number = std::nullopt;
    //     std::chrono::time_point<std::chrono::steady_clock> scheduled_addr_dump = std::chrono::steady_clock::now() + 2min;
    //     std::shared_ptr<ndisc::NetlinkSocket> ip_reader;

    //     void Tick();
    //     void ExpediteAddrDump();

    //     void UpdateAddressList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView &packet);
    // };

    class DeviceRepository
    {

        std::shared_ptr<ndisc::NetlinkSocket> monitor_;

        std::shared_ptr<DeviceReader> device_reader_;
        std::shared_ptr<IpReader> ip_reader_;
        std::map<unsigned int, DeviceData> devices_;

        DeviceRepository(
            std::shared_ptr<NetlinkSocket> monitor_socket,
            std::shared_ptr<DeviceReader> device_reader,
            std::shared_ptr<IpReader> ip_reader) : monitor_(std::move(monitor_socket)),
                                                   device_reader_(std::move(device_reader)),
                                                   ip_reader_(std::move(ip_reader)) {}

    public:
        DeviceRepository(const DeviceRepository &) = delete;
        DeviceRepository(DeviceRepository &&) = default;
        DeviceRepository &operator=(const DeviceRepository &) = delete;
        DeviceRepository &operator=(DeviceRepository &&) = default;
        ~DeviceRepository() {}

        static std::expected<std::unique_ptr<DeviceRepository>, int> Create(EventManager &manager);

        void Tick();

        void HandleMonitorPackets(ndisc::NetlinkPacketView packet);

        void HandleLinkPacket(const LinkView &);

        void HandleAddressPacket(const AddrView &);

        const std::map<unsigned int, DeviceData> &GetDevices() const { return devices_; }

        bool AreReadersIdle()
        {
            return device_reader_->IsReaderIdle() && ip_reader_->IsReaderIdle();
        }

        // void UpdateDeviceList(ndisc::NetlinkPacketView packet);

        // void UpdateAddressList(ndisc::NetlinkPacketView &packet);
    };

} // namespace ndisc
#endif // DEVICE_REPOSITORY_HH