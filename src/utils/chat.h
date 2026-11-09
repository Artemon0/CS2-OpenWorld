#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "ui/ui_adapter.h"

class CPlayerSlot;

// Per-player chat and console output for the OW plugin. Prefer this over calling
// IVEngineServer2::ClientPrintf directly: one call site keeps the channel, the length limit and
// the fallback behaviour consistent for every module.
//
// Every function is safe to call from the game thread at any time. They return false (and do
// nothing) when the engine interface is missing, the slot is invalid, the text is empty or too
// long, or the player is not connected. Nothing is queued or retried.
namespace utils::chat
{
	// Longest payload accepted before delivery. Longer text is rejected, not truncated, so a
	// formatting mistake is visible instead of silently losing the end of the message.
	constexpr std::size_t MaxMessageBytes = 512;

	// Chat in the player's chat window, only visible to `slot`. Add your own prefix, e.g.
	// "[Open World] " + text.
	bool ToPlayer(CPlayerSlot slot, std::string_view text);

	// Console output for `slot` (HUD_PRINTCONSOLE); the engine client's console is the same place a
	// failed send falls back to.
	bool ToConsole(CPlayerSlot slot, std::string_view text);

	// Notification area / center screen for `slot`. Center text is transient and can be cleared by
	// the HUD renderer, so use it for short-lived status only.
	bool ToCenter(CPlayerSlot slot, std::string_view text);
	bool ToNotification(CPlayerSlot slot, std::string_view text);

	// A single message on any channel of ow::ui::UITextChannel. Fails with `false` for an unknown
	// channel.
	bool ToChannel(CPlayerSlot slot, ow::ui::UITextChannel channel, std::string_view text);

	// Strips control characters (including the newline that would end a console line) so a player
	// name or command argument cannot forge extra console lines. Returns false if the text contains
	// a NUL byte, which the engine would treat as the end of the string.
	bool Sanitize(std::string& text);
} // namespace utils::chat
