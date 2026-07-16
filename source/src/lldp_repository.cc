#include "lldp_repository.hh"

namespace ndisc
{
    void LldpRepository::CheckSocketForTxReady(unsigned int idx)
    {
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            if (!device_data.lldp_sender.has_value() && device_data.interface_name.has_value())
            {
                // device_data.lldp_sender = ndisc::LldpSender::Create();
                std::optional<ndisc::LldpSender> sender = ndisc::LldpSender::Create();
                if (sender.has_value())
                {
                    device_data.lldp_sender = std::move(*sender);
                }
            }
        }
    }

    void LldpRepository::MarkChangedLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].LocalChangeDetected();
        }
    }

    void LldpRepository::DeleteLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].EndTransmission();
        }
        current_state.erase(idx);
    }

    void LldpRepository::CreateLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            device_data.NewNeighbour();
        }
    }

    void LldpRepository::UpdateState(std::map<unsigned int, ndisc::DeviceData> &new_state)
    {
        for (auto &[index, new_device_state] : new_state)
        {
            if (current_state.contains(index))
            {
                bool any_changed = false;
                ndisc::DeviceData &device_state = current_state.at(index);
                if (device_state.interface_name != new_device_state.interface_name)
                {
                    any_changed = true;
                    device_state.interface_name = new_device_state.interface_name;
                }
                if (device_state.ip_address != new_device_state.ip_address)
                {
                    any_changed = true;
                    device_state.ip_address = new_device_state.ip_address;
                }
                if (any_changed)
                {
                    MarkChangedLldpStateMachine(index);
                }
            }
            else
            {
                current_state[index] = std::move(new_device_state);
                CreateLldpStateMachine(index);
            }
        }
        std::vector<unsigned int> deletables{};
        for (auto &[index, current_device_state] : current_state)
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
        for (auto &[idx, device] : current_state)
        {
            device.Tick();
        }
    }

    void lldpStateUpdater(LldpRepository &lldp, DeviceRepository &repository)
    {
        if (
            repository.device_reader.device_reader_state == ReaderState::IDLE &&
            repository.ip_reader.ip_reader_state == ReaderState::IDLE)
        {
            lldp.UpdateState(repository.devices);
        }
    }
} // namespace ndisc