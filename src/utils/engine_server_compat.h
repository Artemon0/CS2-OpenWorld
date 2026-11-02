#pragma once

#include "eiface.h"

#include <cstddef>

namespace utils
{
// The running CS2 server exposes one additional IVEngineServer vtable entry
// before GetAppID compared with the bundled SDK interface.
inline constexpr std::size_t kEngineServerGetClientSteamIdSlot = 70;
inline constexpr std::size_t kEngineServerIsClientFullyAuthenticatedSlot = 75;

inline const CSteamID* GetClientSteamID(IVEngineServer2* engine, CPlayerSlot slot)
{
	if (!engine || !slot.IsValid())
	{
		return nullptr;
	}

	using Method = const CSteamID* (*)(IVEngineServer2*, CPlayerSlot);
	void** vtable = *reinterpret_cast<void***>(engine);
	return reinterpret_cast<Method>(vtable[kEngineServerGetClientSteamIdSlot])(engine, slot);
}

inline bool IsClientFullyAuthenticated(IVEngineServer2* engine, CPlayerSlot slot)
{
	if (!engine || !slot.IsValid())
	{
		return false;
	}

	using Method = bool (*)(IVEngineServer2*, CPlayerSlot);
	void** vtable = *reinterpret_cast<void***>(engine);
	return reinterpret_cast<Method>(vtable[kEngineServerIsClientFullyAuthenticatedSlot])(engine, slot);
}
} // namespace utils
