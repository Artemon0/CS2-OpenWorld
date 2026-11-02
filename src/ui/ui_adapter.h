#pragma once

#include <cstdint>
#include <string_view>

#include "playerslot.h"

namespace ow::ui
{
enum class UIStatus
{
	Success,
	Unsupported,
	Unavailable,
	InvalidPlayer,
	InvalidPayload
};

enum class UITextChannel
{
	Notification,
	Console,
	Chat,
	Center
};

struct UICapabilities
{
	bool textNotifications {};
	bool perPlayerDelivery {};
	bool customViews {};
	bool images {};
	bool animation {};
	bool keyboardInput {};
	bool mouseInput {};
	bool fullscreen {};
};

class IUIAdapter
{
public:
	virtual ~IUIAdapter() = default;
	virtual UICapabilities GetCapabilities() const = 0;
	virtual UIStatus ShowText(CPlayerSlot slot, UITextChannel channel, std::string_view text) = 0;

	virtual UIStatus OpenView(CPlayerSlot, std::string_view) { return UIStatus::Unsupported; }
	virtual UIStatus CloseView(CPlayerSlot, std::string_view) { return UIStatus::Unsupported; }
	virtual UIStatus UpdateViewData(CPlayerSlot, std::string_view, std::string_view)
	{
		return UIStatus::Unsupported;
	}
	virtual UIStatus HandleAction(CPlayerSlot, std::string_view, std::string_view)
	{
		return UIStatus::Unsupported;
	}
};
} // namespace ow::ui
