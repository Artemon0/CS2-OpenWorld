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
} // namespace ow::ui
