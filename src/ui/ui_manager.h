#pragma once

#include "ui_adapter.h"

namespace ow::ui
{
class UIManager
{
public:
	bool RegisterAdapter(IUIAdapter& adapter);
	bool UnregisterAdapter(IUIAdapter& adapter);

	UICapabilities GetCapabilities() const;
	UIStatus ShowText(CPlayerSlot slot, UITextChannel channel, std::string_view text) const;

private:
	IUIAdapter* adapter {};
};
} // namespace ow::ui
