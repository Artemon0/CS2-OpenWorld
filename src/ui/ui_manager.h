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
	UIStatus OpenView(CPlayerSlot slot, std::string_view viewId) const;
	UIStatus CloseView(CPlayerSlot slot, std::string_view viewId) const;
	UIStatus UpdateViewData(CPlayerSlot slot, std::string_view viewId, std::string_view data) const;
	UIStatus HandleAction(CPlayerSlot slot, std::string_view viewId, std::string_view action) const;

private:
	IUIAdapter* adapter {};
};
} // namespace ow::ui
