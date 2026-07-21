#ifndef DEVICE_REPOSITORY_HH
#define DEVICE_REPOSITORY_HH

#include <chrono>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "device_reader.hh"
#include "ip_reader.hh"
#include "netlink_monitor.hh"

using namespace std::chrono_literals;

namespace ndisc
{

    class DeviceRepository
    {
        int sync_timeout_ = -1;
        std::shared_ptr<netlink::NetlinkSocket> monitor_;

        std::unique_ptr<netlink::DeviceReader> device_reader_;
        std::unique_ptr<netlink::IpReader> ip_reader_;
        std::map<unsigned int, netlink::DeviceData> devices_;
        std::optional<std::function<void(const std::map<unsigned int, netlink::DeviceData> &)>> synchronization_callback_;

        void BindCallbacks()
        {
            if (device_reader_ != nullptr)
            {
                device_reader_->SetDeviceUpdateCallback([this](const netlink::LinkView &link)
                                                        { this->HandleLinkPacket(link); });
                device_reader_->SetEndOfDumpCallback([this]()
                                                     { this->DeviceReaderFinished(); });
                device_reader_->SetDumpErroredCallback([this]()
                                                       { this->DeviceReaderErrored(); });
            }
            if (ip_reader_ != nullptr)
            {
                ip_reader_->SetAddressUpdateCallback([this](const netlink::AddrView &addr)
                                                     { this->HandleAddressPacket(addr); });
                ip_reader_->SetEndOfIpDumpCallback([this]()
                                                   { this->IpReaderFinished(); });
                ip_reader_->SetDumpErroredCallback([this]()
                                                   { this->IpReaderErrored(); });
            }
            if (monitor_ != nullptr)
            {
                monitor_->SetCallback([this](const netlink::NetlinkPacketView &packet)
                                      { this->HandleMonitorPackets(packet); });
            }
        }

        void ClearCallbacks()
        {
            if (device_reader_ != nullptr)
            {
                device_reader_->ClearDeviceUpdateCallback();
                device_reader_->ClearDumpErroredCallback();
                device_reader_->ClearEndOfDumpCallback();
            }
            if (ip_reader_ != nullptr)
            {
                ip_reader_->ClearAddressUpdateCallback();
                ip_reader_->ClearDumpErroredCallback();
                ip_reader_->ClearEndOfIpDumpCallback();
            }
            if (monitor_ != nullptr)
            {
                monitor_->ClearCallback();
            }
        }

        void ScheduleResync();
        void ScheduleExpediteResync();
        void RequestDeviceDump();
        void RequestIpDump();

        void DeviceReaderFinished();
        void DeviceReaderErrored();

        void IpReaderFinished();
        void IpReaderErrored();

        DeviceRepository(
            std::shared_ptr<netlink::NetlinkSocket> monitor_socket,
            std::unique_ptr<netlink::DeviceReader> device_reader,
            std::unique_ptr<netlink::IpReader> ip_reader) : monitor_(std::move(monitor_socket)),
                                                            device_reader_(std::move(device_reader)),
                                                            ip_reader_(std::move(ip_reader))
        {
            BindCallbacks();
        }

    public:
        DeviceRepository(const DeviceRepository &) = delete;
        DeviceRepository(DeviceRepository &&other) noexcept : monitor_(std::move(other.monitor_)),
                                                              device_reader_(std::move(other.device_reader_)),
                                                              ip_reader_(std::move(other.ip_reader_)),
                                                              devices_(std::move(other.devices_))
        {
            other.monitor_.reset();
            other.device_reader_.reset();
            other.ip_reader_.reset();
            other.devices_.clear();
            BindCallbacks();
        }
        DeviceRepository &operator=(const DeviceRepository &) = delete;
        DeviceRepository &operator=(DeviceRepository &&other)
        {
            ClearCallbacks();
            other.ClearCallbacks();
            monitor_ = std::move(other.monitor_);
            device_reader_ = std::move(other.device_reader_);
            ip_reader_ = std::move(other.ip_reader_);
            devices_ = std::move(other.devices_);
            other.monitor_.reset();
            other.device_reader_.reset();
            other.ip_reader_.reset();
            other.devices_.clear();
            BindCallbacks();
            return *this;
        };
        ~DeviceRepository()
        {
            ClearCallbacks();
        }

        static std::expected<std::unique_ptr<DeviceRepository>, int> Create(EventManager &manager);

        void Tick();

        void HandleMonitorPackets(const netlink::NetlinkPacketView &packet);

        void HandleLinkPacket(const netlink::LinkView &);

        void HandleAddressPacket(const netlink::AddrView &);

        const std::map<unsigned int, netlink::DeviceData> &GetDevices() const { return devices_; }

        void SetSyncCallback(std::function<void(const std::map<unsigned int, netlink::DeviceData> &)> callback)
        {
            synchronization_callback_ = std::move(callback);
        }

        void ClearSyncCallback()
        {
            synchronization_callback_ = std::nullopt;
        }
    };

} // namespace ndisc
#endif // DEVICE_REPOSITORY_HH