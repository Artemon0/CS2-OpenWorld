#include "input_manager.h"

#include <algorithm>
#include <utility>

namespace ow::ui
{
InputManager::InputManager()
{
	BindAction(InputContext::Gameplay, ScoreButton, InputAction::OpenMap);
	BindAction(InputContext::Gameplay, UseButton, InputAction::OpenInventory);
}

void InputManager::SetActionHandler(ActionHandler handler)
{
	actionHandler = std::move(handler);
}

bool InputManager::BindAction(InputContext context, std::uint64_t buttonMask, InputAction action)
{
	if (!IsValidContext(context) || context == InputContext::Disabled || buttonMask == 0 || !IsValidAction(action))
	{
		return false;
	}
	const auto existing = std::find_if(bindings.begin(), bindings.end(), [context, buttonMask](const Binding& binding) {
		return binding.context == context && binding.buttonMask == buttonMask;
	});
	if (existing != bindings.end())
	{
		return false;
	}
	bindings.push_back({context, buttonMask, action});
	return true;
}

bool InputManager::UnbindAction(InputContext context, std::uint64_t buttonMask)
{
	const auto binding = std::find_if(bindings.begin(), bindings.end(), [context, buttonMask](const Binding& candidate) {
		return candidate.context == context && candidate.buttonMask == buttonMask;
	});
	if (binding == bindings.end())
	{
		return false;
	}
	bindings.erase(binding);
	return true;
}

bool InputManager::SetContext(CPlayerSlot slot, InputContext context)
{
	if (!slot.IsValid() || !IsValidContext(context))
	{
		return false;
	}
	players[slot.Get()].contexts.assign(1, context);
	return true;
}

bool InputManager::PushInputContext(CPlayerSlot slot, InputContext context)
{
	if (!slot.IsValid() || !IsValidContext(context))
	{
		return false;
	}
	players[slot.Get()].contexts.push_back(context);
	return true;
}

bool InputManager::PopInputContext(CPlayerSlot slot)
{
	if (!slot.IsValid())
	{
		return false;
	}
	const auto player = players.find(slot.Get());
	if (player == players.end() || player->second.contexts.size() <= 1)
	{
		return false;
	}
	player->second.contexts.pop_back();
	return true;
}

InputContext InputManager::GetContext(CPlayerSlot slot) const
{
	if (!slot.IsValid())
	{
		return InputContext::Disabled;
	}
	const auto player = players.find(slot.Get());
	return player == players.end() ? InputContext::Gameplay : player->second.contexts.back();
}

std::size_t InputManager::HandleInput(CPlayerSlot slot, const std::uint64_t buttonStates[3])
{
	if (!slot.IsValid() || !buttonStates)
	{
		return 0;
	}

	PlayerState& state = players[slot.Get()];
	const std::uint64_t currentButtons = buttonStates[0];
	if (!state.hasButtonSnapshot)
	{
		state.previousButtons = currentButtons;
		state.hasButtonSnapshot = true;
		return 0;
	}

	const std::uint64_t newlyPressed = currentButtons & ~state.previousButtons;
	state.previousButtons = currentButtons;
	if (newlyPressed == 0 || state.contexts.back() == InputContext::Disabled || !actionHandler)
	{
		return 0;
	}

	std::vector<InputAction> actions;
	for (const Binding& binding : bindings)
	{
		if (binding.context == state.contexts.back() && (binding.buttonMask & newlyPressed) != 0)
		{
			actions.push_back(binding.action);
		}
	}

	const ActionHandler handler = actionHandler;
	for (const InputAction action : actions)
	{
		handler(slot, action);
	}
	return actions.size();
}

bool InputManager::DispatchAction(CPlayerSlot slot, InputAction action)
{
	if (!slot.IsValid() || !IsValidAction(action) || !actionHandler)
	{
		return false;
	}
	const InputContext context = GetContext(slot);
	if (context == InputContext::Disabled || !IsActionAvailable(context, action))
	{
		return false;
	}

	const ActionHandler handler = actionHandler;
	handler(slot, action);
	return true;
}

void InputManager::RemovePlayer(CPlayerSlot slot)
{
	players.erase(slot.Get());
}

void InputManager::ClearAll()
{
	players.clear();
}

bool InputManager::IsValidContext(InputContext context)
{
	return context <= InputContext::Disabled;
}

bool InputManager::IsValidAction(InputAction action)
{
	return action == InputAction::OpenMap || action == InputAction::OpenInventory;
}

bool InputManager::IsActionAvailable(InputContext context, InputAction action) const
{
	return std::any_of(bindings.begin(), bindings.end(), [context, action](const Binding& binding) {
		return binding.context == context && binding.action == action;
	});
}
} // namespace ow::ui
