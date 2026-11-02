#pragma once

#include "playerslot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ow::ui
{
enum class InputContext : std::uint8_t
{
	Gameplay,
	Dialog,
	Inventory,
	Map,
	Vehicle,
	Menu,
	Disabled
};

enum class InputAction : std::uint8_t
{
	OpenMap,
	OpenInventory
};

class InputManager
{
public:
	using ActionHandler = std::function<void(CPlayerSlot, InputAction)>;

	static constexpr std::uint64_t UseButton = std::uint64_t{1} << 5;
	static constexpr std::uint64_t ScoreButton = std::uint64_t{1} << 33;

	InputManager();

	void SetActionHandler(ActionHandler handler);
	bool BindAction(InputContext context, std::uint64_t buttonMask, InputAction action);
	bool UnbindAction(InputContext context, std::uint64_t buttonMask);
	bool SetContext(CPlayerSlot slot, InputContext context);
	bool PushInputContext(CPlayerSlot slot, InputContext context);
	bool PopInputContext(CPlayerSlot slot);
	InputContext GetContext(CPlayerSlot slot) const;

	std::size_t HandleInput(CPlayerSlot slot, const std::uint64_t buttonStates[3]);
	bool DispatchAction(CPlayerSlot slot, InputAction action);
	void RemovePlayer(CPlayerSlot slot);
	void ClearAll();

private:
	struct Binding
	{
		InputContext context;
		std::uint64_t buttonMask;
		InputAction action;
	};

	struct PlayerState
	{
		std::vector<InputContext> contexts {InputContext::Gameplay};
		std::uint64_t previousButtons {};
		bool hasButtonSnapshot {};
	};

	static bool IsValidContext(InputContext context);
	static bool IsValidAction(InputAction action);
	bool IsActionAvailable(InputContext context, InputAction action) const;

	ActionHandler actionHandler;
	std::vector<Binding> bindings;
	std::unordered_map<int, PlayerState> players;
};
} // namespace ow::ui
