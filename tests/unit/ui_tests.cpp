#include "ui/hud/hud_system.h"
#include "ui/input/input_manager.h"
#include "ui/ui_manager.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct TextRecord
{
	CPlayerSlot slot {-1};
	ow::ui::UITextChannel channel {ow::ui::UITextChannel::Chat};
	std::string text;
};

class FakeUIAdapter final : public ow::ui::IUIAdapter
{
public:
	ow::ui::UICapabilities GetCapabilities() const override
	{
		ow::ui::UICapabilities capabilities;
		capabilities.textNotifications = true;
		capabilities.perPlayerDelivery = true;
		return capabilities;
	}

	ow::ui::UIStatus ShowText(CPlayerSlot slot, ow::ui::UITextChannel channel, std::string_view text) override
	{
		records.push_back({slot, channel, std::string(text)});
		return ow::ui::UIStatus::Success;
	}

	std::vector<TextRecord> records;
};

bool TestUIManager()
{
	ow::ui::UIManager manager;
	FakeUIAdapter adapter;
	FakeUIAdapter otherAdapter;
	if (!manager.RegisterAdapter(adapter) || manager.RegisterAdapter(otherAdapter) ||
		manager.ShowText(CPlayerSlot(1), ow::ui::UITextChannel::Chat, "hello") != ow::ui::UIStatus::Success ||
		manager.ShowText(CPlayerSlot(1), ow::ui::UITextChannel::Notification, "notice") != ow::ui::UIStatus::Success ||
		manager.ShowText(CPlayerSlot(1), ow::ui::UITextChannel::Console, "console") != ow::ui::UIStatus::Success ||
		manager.ShowText(CPlayerSlot(1), ow::ui::UITextChannel::Center, "center") != ow::ui::UIStatus::Success ||
		manager.ShowText(CPlayerSlot(1), static_cast<ow::ui::UITextChannel>(255), "invalid") !=
			ow::ui::UIStatus::InvalidPayload ||
		manager.ShowText(CPlayerSlot(-1), ow::ui::UITextChannel::Chat, "bad slot") != ow::ui::UIStatus::InvalidPlayer ||
		manager.OpenView(CPlayerSlot(1), "inventory") != ow::ui::UIStatus::Unsupported || adapter.records.size() != 4 ||
		adapter.records[0].slot.Get() != 1 || adapter.records[0].text != "hello" ||
		adapter.records[1].channel != ow::ui::UITextChannel::Notification || adapter.records[2].channel != ow::ui::UITextChannel::Console ||
		adapter.records[3].channel != ow::ui::UITextChannel::Center)
	{
		return false;
	}
	return manager.UnregisterAdapter(adapter) && manager.ShowText(CPlayerSlot(1), ow::ui::UITextChannel::Chat, "") ==
													ow::ui::UIStatus::Unavailable;
}

bool TestInputManager()
{
	ow::ui::InputManager input;
	std::vector<std::pair<int, ow::ui::InputAction>> actions;
	input.SetActionHandler([&actions](CPlayerSlot slot, ow::ui::InputAction action) {
		actions.emplace_back(slot.Get(), action);
	});

	const std::uint64_t usePressed[3] {ow::ui::InputManager::UseButton, 0, 0};
	const std::uint64_t released[3] {};
	const std::uint64_t scorePressed[3] {ow::ui::InputManager::ScoreButton, 0, 0};
	if (input.HandleInput(CPlayerSlot(2), usePressed) != 0 ||
		input.HandleInput(CPlayerSlot(2), usePressed) != 0 ||
		input.HandleInput(CPlayerSlot(2), released) != 0 ||
		input.HandleInput(CPlayerSlot(2), usePressed) != 1 || actions.size() != 1 ||
		actions.back() != std::make_pair(2, ow::ui::InputAction::OpenInventory))
	{
		return false;
	}

	if (input.HandleInput(CPlayerSlot(2), released) != 0 ||
		input.HandleInput(CPlayerSlot(2), scorePressed) != 1 || actions.size() != 2 ||
		actions.back() != std::make_pair(2, ow::ui::InputAction::OpenMap) ||
		!input.SetContext(CPlayerSlot(2), ow::ui::InputContext::Dialog) ||
		!input.PushInputContext(CPlayerSlot(2), ow::ui::InputContext::Menu) ||
		input.GetContext(CPlayerSlot(2)) != ow::ui::InputContext::Menu ||
		!input.PopInputContext(CPlayerSlot(2)) ||
		input.GetContext(CPlayerSlot(2)) != ow::ui::InputContext::Dialog ||
		input.DispatchAction(CPlayerSlot(2), ow::ui::InputAction::OpenInventory) ||
		!input.SetContext(CPlayerSlot(2), ow::ui::InputContext::Gameplay) ||
		!input.DispatchAction(CPlayerSlot(2), ow::ui::InputAction::OpenMap) || actions.size() != 3)
	{
		return false;
	}

	if (!input.BindAction(ow::ui::InputContext::Menu, ow::ui::InputManager::UseButton,
						  ow::ui::InputAction::OpenInventory) ||
		input.BindAction(ow::ui::InputContext::Menu, ow::ui::InputManager::UseButton,
					 ow::ui::InputAction::OpenMap) ||
		!input.PushInputContext(CPlayerSlot(2), ow::ui::InputContext::Menu) ||
		!input.DispatchAction(CPlayerSlot(2), ow::ui::InputAction::OpenInventory) || actions.size() != 4 ||
		actions.back() != std::make_pair(2, ow::ui::InputAction::OpenInventory) ||
		!input.PopInputContext(CPlayerSlot(2)) ||
		!input.UnbindAction(ow::ui::InputContext::Menu, ow::ui::InputManager::UseButton) ||
		input.PopInputContext(CPlayerSlot(2)) || input.HandleInput(CPlayerSlot(-1), usePressed) != 0)
	{
		return false;
	}

	input.RemovePlayer(CPlayerSlot(2));
	return input.GetContext(CPlayerSlot(2)) == ow::ui::InputContext::Gameplay &&
		   input.HandleInput(CPlayerSlot(2), usePressed) == 0;
}

bool TestHUDSystem()
{
	ow::ui::UIManager manager;
	FakeUIAdapter adapter;
	if (!manager.RegisterAdapter(adapter))
	{
		return false;
	}
	ow::ui::HUDSystem hud(manager);
	const std::uint32_t defaultAndCombat = ow::ui::HUDProfileFlag(ow::ui::HUDProfile::Default) |
										 ow::ui::HUDProfileFlag(ow::ui::HUDProfile::Combat);
	if (!hud.RegisterHUDElement({"health", "Health", "100", ow::ui::AllHUDProfiles})
		|| !hud.RegisterHUDElement({"armor", "Armor", "50", defaultAndCombat})
		|| !hud.RegisterHUDElement({"vehicle", "Speed", "0 km/h", ow::ui::HUDProfileFlag(ow::ui::HUDProfile::Vehicle)})
		|| hud.RegisterHUDElement({"invalid", "Bad\nLabel", "", ow::ui::AllHUDProfiles}) || !hud.UpdateHUDElement(CPlayerSlot(3), "health", "87")
		|| hud.IsHUDEnabled(CPlayerSlot(3)) || !adapter.records.empty())
	{
		return false;
	}
	if (!hud.SetHUDEnabled(CPlayerSlot(3), true) || !hud.IsHUDEnabled(CPlayerSlot(3)) || adapter.records.empty()
		|| adapter.records.back().channel != ow::ui::UITextChannel::Center || adapter.records.back().text != "Armor: 50\nHealth: 87")
	{
		return false;
	}

	if (!hud.HideHUDElement(CPlayerSlot(3), "armor") || !hud.SetHUDProfile(CPlayerSlot(3), ow::ui::HUDProfile::Vehicle))
	{
		return false;
	}
	const auto vehicleElements = hud.GetVisibleElements(CPlayerSlot(3));
	if (vehicleElements.size() != 2 || vehicleElements[0].id != "health" || vehicleElements[1].id != "vehicle")
	{
		return false;
	}
	if (!hud.RestoreHUDProfile(CPlayerSlot(3)) || hud.GetHUDProfile(CPlayerSlot(3)) != ow::ui::HUDProfile::Default)
	{
		return false;
	}
	const auto restoredElements = hud.GetVisibleElements(CPlayerSlot(3));
	if (restoredElements.size() != 2 || restoredElements[0].id != "armor" || restoredElements[1].id != "health")
	{
		return false;
	}

	const auto refreshTime = ow::ui::HUDSystem::Clock::now() + std::chrono::seconds(2);
	const std::size_t beforeRefresh = adapter.records.size();
	if (hud.Refresh(refreshTime) != 1 || adapter.records.size() != beforeRefresh + 1 || adapter.records.back().text != "Armor: 50\nHealth: 87"
		|| hud.Refresh(refreshTime + std::chrono::milliseconds(500)) != 0)
	{
		return false;
	}
	if (!hud.SetHUDEnabled(CPlayerSlot(3), false) || hud.IsHUDEnabled(CPlayerSlot(3)) || !hud.GetVisibleElements(CPlayerSlot(3)).empty())
	{
		return false;
	}
	hud.RemovePlayer(CPlayerSlot(3));
	if (!adapter.records.back().text.empty() || !hud.SetHUDEnabled(CPlayerSlot(3), true))
	{
		return false;
	}
	const std::size_t beforeDisconnectCleanup = adapter.records.size();
	hud.RemovePlayer(CPlayerSlot(3), false);
	return adapter.records.size() == beforeDisconnectCleanup && !hud.HideHUDElement(CPlayerSlot(3), "missing");
}
} // namespace

int main()
{
	return TestUIManager() && TestInputManager() && TestHUDSystem() ? 0 : 1;
}
