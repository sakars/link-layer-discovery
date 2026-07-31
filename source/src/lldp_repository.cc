#include "lldp_repository.hh"

#include <iostream>

namespace ndisc
{
    std::expected<LldpRepository, int> LldpRepository::Create(ndisc::EventManager &manager)
    {
        std::expected<std::unique_ptr<ndisc::DeviceRepository>, int> device_repository = ndisc::DeviceRepository::Create(manager);
        if (!device_repository.has_value())
        {
            return std::unexpected(device_repository.error());
        }

        OwnedFileDescriptor eth_broadcast_fd{socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))};
        if (!eth_broadcast_fd.IsValid())
        {
            return std::unexpected(errno);
        }
        return LldpRepository(std::move(eth_broadcast_fd), std::move(*device_repository));
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
            lldp::LldpSender &lldp_sender = current_state_.at(idx);
            lldp_sender.NewNeighbour();
        }
    }

    void LldpRepository::UpdateState(const std::map<unsigned int, netlink::DeviceData> &new_state)
    {
        for (const auto &[index, new_device_state] : new_state)
        {
            if (current_state_.contains(index))
            {
                lldp::LldpSender &lldp_sender = current_state_.at(index);
                lldp_sender.Update(new_device_state);
            }
            else
            {
                current_state_.emplace(index, lldp::LldpSender(ethernet_broadcast_socket_, new_device_state));
                CreateLldpStateMachine(index);
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

    void LldpRepository::Tick(const uint64_t &delta_seconds)
    {
        if (device_repository_ != nullptr)
        {
            device_repository_->Tick(delta_seconds);
        }
        for (auto &[idx, device] : current_state_)
        {
            device.Tick(delta_seconds);
        }
    }

} // namespace ndisc