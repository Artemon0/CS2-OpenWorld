#include "hud_system.h"

#include <utility>

namespace ow::ui
{
bool HUDSystem::RegisterHUDElement(HUDElementDefinition definition)
{
	if (!IsValidText(definition.id, 64) || definition.id.empty() ||
		definition.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
			std::string::npos ||
		!IsValidText(definition.label, 80) || definition.label.empty() ||
		!IsValidText(definition.defaultValue, 256) || (definition.visibleProfiles & ~AllHUDProfiles) != 0 ||
		definitions.find(definition.id) != definitions.end())
	{
		return false;
	}
	const std::string id = definition.id;
	definitions.emplace(id, std::move(definition));
	for (auto& [slot, state] : players)
	{
		(void)slot;
		state.dirty = true;
	}
	return true;
}

bool HUDSystem::ShowHUDElement(CPlayerSlot slot, std::string_view elementId)
{
	if (!slot.IsValid() || elementId.size() > 64 || definitions.find(std::string(elementId)) == definitions.end())
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	state.visibilityOverrides[std::string(elementId)] = true;
	state.dirty = true;
	Render(slot);
	return true;
}

bool HUDSystem::HideHUDElement(CPlayerSlot slot, std::string_view elementId)
{
	if (!slot.IsValid() || elementId.size() > 64 || definitions.find(std::string(elementId)) == definitions.end())
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	state.visibilityOverrides[std::string(elementId)] = false;
	state.dirty = true;
	Render(slot);
	return true;
}

bool HUDSystem::UpdateHUDElement(CPlayerSlot slot, std::string_view elementId, std::string_view value)
{
	if (!slot.IsValid() || elementId.size() > 64 || definitions.find(std::string(elementId)) == definitions.end() ||
		!IsValidText(value, 256))
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	const std::string key(elementId);
	const auto current = state.values.find(key);
	if (current != state.values.end() && current->second == value)
	{
		return true;
	}
	state.values.insert_or_assign(key, std::string(value));
	state.dirty = true;
	Render(slot);
	return true;
}

bool HUDSystem::SetHUDProfile(CPlayerSlot slot, HUDProfile profile)
{
	if (!slot.IsValid() || !IsValidProfile(profile))
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	if (state.profile != profile)
	{
		state.profile = profile;
		state.dirty = true;
		Render(slot);
	}
	return true;
}

bool HUDSystem::RestoreHUDProfile(CPlayerSlot slot)
{
	if (!slot.IsValid())
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	state.profile = HUDProfile::Default;
	state.visibilityOverrides.clear();
	state.dirty = true;
	Render(slot);
	return true;
}

bool HUDSystem::SetHUDEnabled(CPlayerSlot slot, bool enabled)
{
	if (!slot.IsValid())
	{
		return false;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	if (state.enabled != enabled)
	{
		state.enabled = enabled;
		state.dirty = true;
		state.nextRefresh = Clock::time_point{};
		Render(slot);
	}
	return true;
}

HUDProfile HUDSystem::GetHUDProfile(CPlayerSlot slot) const
{
	const auto found = players.find(slot.Get());
	return found == players.end() ? HUDProfile::Default : found->second.profile;
}

bool HUDSystem::IsHUDEnabled(CPlayerSlot slot) const
{
	const auto found = players.find(slot.Get());
	return slot.IsValid() && found != players.end() && found->second.enabled;
}

std::vector<HUDElementValue> HUDSystem::GetVisibleElements(CPlayerSlot slot) const
{
	if (!slot.IsValid())
	{
		return {};
	}
	const auto player = players.find(slot.Get());
	const PlayerState defaultState;
	const PlayerState& state = player == players.end() ? defaultState : player->second;
	if (!state.enabled)
	{
		return {};
	}

	std::vector<HUDElementValue> result;
	for (const auto& [id, definition] : definitions)
	{
		if (!IsVisible(definition, state))
		{
			continue;
		}
		const auto value = state.values.find(id);
		result.push_back({id, definition.label,
			value == state.values.end() ? definition.defaultValue : value->second});
	}
	return result;
}

UIStatus HUDSystem::Render(CPlayerSlot slot, Clock::time_point now)
{
	if (!slot.IsValid())
	{
		return UIStatus::InvalidPlayer;
	}
	PlayerState& state = GetOrCreatePlayerState(slot);
	const std::string text = BuildText(state);
	if (text.empty() && state.lastRenderedText.empty())
	{
		state.dirty = false;
		return UIStatus::Success;
	}
	if (text.size() > MaxRenderedBytes)
	{
		state.dirty = true;
		state.nextRefresh = now + RefreshInterval;
		return UIStatus::InvalidPayload;
	}
	if (state.hasRendered && now < state.nextRefresh)
	{
		state.dirty = state.dirty || text != state.lastRenderedText;
		return UIStatus::Success;
	}

	const UIStatus status = uiManager.ShowText(slot, UITextChannel::Center, text);
	state.nextRefresh = now + RefreshInterval;
	if (status == UIStatus::Success)
	{
		state.lastRenderedText = text;
		state.hasRendered = true;
		state.dirty = false;
	}
	else
	{
		state.dirty = true;
	}
	return status;
}

std::size_t HUDSystem::Refresh(Clock::time_point now)
{
	std::size_t sent = 0;
	for (auto& [slot, state] : players)
	{
		if ((state.enabled || !state.lastRenderedText.empty()) &&
			(state.dirty || !state.lastRenderedText.empty()) && now >= state.nextRefresh &&
			Render(CPlayerSlot(slot), now) == UIStatus::Success)
		{
			++sent;
		}
	}
	return sent;
}

void HUDSystem::RemovePlayer(CPlayerSlot slot, bool clearDisplay)
{
	if (!slot.IsValid())
	{
		return;
	}
	const auto found = players.find(slot.Get());
	if (found == players.end())
	{
		return;
	}
	if (clearDisplay && found->second.hasRendered && !found->second.lastRenderedText.empty())
	{
		uiManager.ShowText(slot, UITextChannel::Center, {});
	}
	players.erase(found);
}

void HUDSystem::ClearAll()
{
	for (const auto& [slot, state] : players)
	{
		if (state.hasRendered && !state.lastRenderedText.empty())
		{
			uiManager.ShowText(CPlayerSlot(slot), UITextChannel::Center, {});
		}
	}
	players.clear();
}

bool HUDSystem::IsValidText(std::string_view text, std::size_t maxBytes)
{
	return text.size() <= maxBytes && text.find('\0') == std::string_view::npos &&
		   text.find('\r') == std::string_view::npos && text.find('\n') == std::string_view::npos;
}

bool HUDSystem::IsValidProfile(HUDProfile profile)
{
	return profile < HUDProfile::Count;
}

bool HUDSystem::IsVisible(const HUDElementDefinition& definition, const PlayerState& state) const
{
	const auto override = state.visibilityOverrides.find(definition.id);
	if (override != state.visibilityOverrides.end())
	{
		return override->second;
	}
	return (definition.visibleProfiles & HUDProfileFlag(state.profile)) != 0;
}

HUDSystem::PlayerState& HUDSystem::GetOrCreatePlayerState(CPlayerSlot slot)
{
	return players.try_emplace(slot.Get()).first->second;
}

std::string HUDSystem::BuildText(const PlayerState& state) const
{
	if (!state.enabled)
	{
		return {};
	}
	std::string result;
	for (const auto& [id, definition] : definitions)
	{
		if (!IsVisible(definition, state))
		{
			continue;
		}
		const auto value = state.values.find(id);
		const std::string_view displayedValue = value == state.values.end() ? definition.defaultValue : value->second;
		if (!result.empty())
		{
			result.push_back('\n');
		}
		result += definition.label;
		if (!displayedValue.empty())
		{
			result += ": ";
			result += displayedValue;
		}
		if (result.size() > MaxRenderedBytes)
		{
			return result;
		}
	}
	return result;
}
} // namespace ow::ui
