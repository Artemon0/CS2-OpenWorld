#pragma once

#include "ui/ui_manager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ow::ui
{
enum class HUDProfile : std::uint8_t
{
	Default,
	Exploration,
	Combat,
	Vehicle,
	Dialog,
	Cinematic,
	Count
};

constexpr std::uint32_t HUDProfileFlag(HUDProfile profile)
{
	return std::uint32_t{1} << static_cast<std::uint8_t>(profile);
}

constexpr std::uint32_t AllHUDProfiles = (std::uint32_t{1} << static_cast<std::uint8_t>(HUDProfile::Count)) - 1;

struct HUDElementDefinition
{
	std::string id;
	std::string label;
	std::string defaultValue;
	std::uint32_t visibleProfiles {AllHUDProfiles};
};

struct HUDElementValue
{
	std::string id;
	std::string label;
	std::string value;
};

class HUDSystem
{
public:
	using Clock = std::chrono::steady_clock;
	static constexpr std::size_t MaxRenderedBytes = 1024;
	static constexpr std::chrono::milliseconds RefreshInterval {1000};

	explicit HUDSystem(UIManager& uiManager) : uiManager(uiManager) {}

	bool RegisterHUDElement(HUDElementDefinition definition);
	bool ShowHUDElement(CPlayerSlot slot, std::string_view elementId);
	bool HideHUDElement(CPlayerSlot slot, std::string_view elementId);
	bool UpdateHUDElement(CPlayerSlot slot, std::string_view elementId, std::string_view value);
	bool SetHUDProfile(CPlayerSlot slot, HUDProfile profile);
	bool RestoreHUDProfile(CPlayerSlot slot);
	bool SetHUDEnabled(CPlayerSlot slot, bool enabled);

	HUDProfile GetHUDProfile(CPlayerSlot slot) const;
	bool IsHUDEnabled(CPlayerSlot slot) const;
	std::vector<HUDElementValue> GetVisibleElements(CPlayerSlot slot) const;
	UIStatus Render(CPlayerSlot slot, Clock::time_point now = Clock::now());
	std::size_t Refresh(Clock::time_point now = Clock::now());
	void RemovePlayer(CPlayerSlot slot, bool clearDisplay = true);
	void ClearAll();

private:
	struct PlayerState
	{
		HUDProfile profile {HUDProfile::Default};
		std::unordered_map<std::string, std::string> values;
		std::unordered_map<std::string, bool> visibilityOverrides;
		std::string lastRenderedText;
		Clock::time_point nextRefresh {};
		bool hasRendered {};
		bool dirty {true};
		bool enabled {};
	};

	static bool IsValidText(std::string_view text, std::size_t maxBytes);
	static bool IsValidProfile(HUDProfile profile);
	bool IsVisible(const HUDElementDefinition& definition, const PlayerState& state) const;
	PlayerState& GetOrCreatePlayerState(CPlayerSlot slot);
	std::string BuildText(const PlayerState& state) const;

	UIManager& uiManager;
	std::map<std::string, HUDElementDefinition> definitions;
	std::unordered_map<int, PlayerState> players;
};
} // namespace ow::ui
