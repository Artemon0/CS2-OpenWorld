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
};

class IUIAdapter
{
public:
	virtual ~IUIAdapter() = default;
	virtual UICapabilities GetCapabilities() const = 0;
	virtual UIStatus ShowText(CPlayerSlot slot, UITextChannel channel, std::string_view text) = 0;
};
} // namespace ow::ui
