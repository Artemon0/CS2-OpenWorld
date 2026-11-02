#include "ui_manager.h"

namespace ow::ui
{
bool UIManager::RegisterAdapter(IUIAdapter& newAdapter)
{
	if (adapter && adapter != &newAdapter)
	{
		return false;
	}
	adapter = &newAdapter;
	return true;
}

bool UIManager::UnregisterAdapter(IUIAdapter& currentAdapter)
{
	if (adapter != &currentAdapter)
	{
		return false;
	}
	adapter = nullptr;
	return true;
}

UICapabilities UIManager::GetCapabilities() const
{
	return adapter ? adapter->GetCapabilities() : UICapabilities{};
}

UIStatus UIManager::ShowText(CPlayerSlot slot, UITextChannel channel, std::string_view text) const
{
	if (!adapter)
	{
		return UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	if (channel != UITextChannel::Notification && channel != UITextChannel::Console &&
		channel != UITextChannel::Chat && channel != UITextChannel::Center)
	{
		return UIStatus::InvalidPayload;
	}
	const UICapabilities capabilities = adapter->GetCapabilities();
	if (!capabilities.textNotifications || !capabilities.perPlayerDelivery)
	{
		return UIStatus::Unsupported;
	}
	return adapter->ShowText(slot, channel, text);
}

UIStatus UIManager::OpenView(CPlayerSlot slot, std::string_view viewId) const
{
	if (!adapter)
	{
		return UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	if (viewId.empty())
	{
		return UIStatus::InvalidPayload;
	}
	return adapter->GetCapabilities().customViews ? adapter->OpenView(slot, viewId) : UIStatus::Unsupported;
}

UIStatus UIManager::CloseView(CPlayerSlot slot, std::string_view viewId) const
{
	if (!adapter)
	{
		return UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	if (viewId.empty())
	{
		return UIStatus::InvalidPayload;
	}
	return adapter->GetCapabilities().customViews ? adapter->CloseView(slot, viewId) : UIStatus::Unsupported;
}

UIStatus UIManager::UpdateViewData(CPlayerSlot slot, std::string_view viewId, std::string_view data) const
{
	if (!adapter)
	{
		return UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	if (viewId.empty())
	{
		return UIStatus::InvalidPayload;
	}
	return adapter->GetCapabilities().customViews ? adapter->UpdateViewData(slot, viewId, data) : UIStatus::Unsupported;
}

UIStatus UIManager::HandleAction(CPlayerSlot slot, std::string_view viewId, std::string_view action) const
{
	if (!adapter)
	{
		return UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	if (viewId.empty() || action.empty())
	{
		return UIStatus::InvalidPayload;
	}
	return adapter->GetCapabilities().customViews ? adapter->HandleAction(slot, viewId, action) : UIStatus::Unsupported;
}
} // namespace ow::ui
