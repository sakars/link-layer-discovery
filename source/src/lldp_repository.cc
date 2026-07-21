#include "lldp_repository.hh"

#include <iostream>

namespace ndisc
{
    std::expected<LldpRepository, int> LldpRepository::Create(std::unique_ptr<DeviceRepository> device_repository)
    {
        OwnedFileDescriptor eth_broadcast_fd{socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))};
        if (!eth_broadcast_fd.IsValid())
        {
            return std::unexpected(errno);
        }
        return LldpRepository(std::move(eth_broadcast_fd), std::move(device_repository));
    }

    void LldpRepository::MarkChangedLldpStateMachine(unsigned int idx)
    {
        if (current_state_.contains(idx))
        {
            current_state_.at(idx).LocalChangeDetected();
        }
    }

    void LldpRepository::DeleteLldpStateMachine(unsigned int idx)
    {
        if (current_state_.contains(idx))
        {
            current_state_.at(idx).EndTransmission();
            current_state_.erase(idx);
        }
    }

    void LldpRepository::CreateLldpStateMachine(unsigned int idx)
    {
        if (current_state_.contains(idx))
        {
            netlink::LldpSender &lldp_sender = current_state_.at(idx);
            lldp_sender.NewNeighbour();
        }
    }

    void LldpRepository::UpdateState(const std::map<unsigned int, netlink::DeviceData> &new_state)
    {
        for (const auto &[index, new_device_state] : new_state)
        {
            if (current_state_.contains(index))
            {
                netlink::LldpSender &lldp_sender = current_state_.at(index);
                lldp_sender.Update(new_device_state);
            }
            else
            {
                current_state_.emplace(index, netlink::LldpSender(ethernet_broadcast_socket_, new_device_state));
                CreateLldpStateMachine(index);
                break;
            }
        }
        std::vector<unsigned int> deletables{};
        for (auto &[index, current_device_state] : current_state_)
        {
            if (!new_state.contains(index))
            {
                deletables.push_back(index);
            }
        }
        for (const unsigned int idx : deletables)
        {
            DeleteLldpStateMachine(idx);
        }
    }

    void LldpRepository::Tick()
    {
        if (device_repository_ != nullptr)
        {
            device_repository_->Tick();
        }
        for (auto &[idx, device] : current_state_)
        {
            device.Tick();
        }
    }

} // namespace ndisc