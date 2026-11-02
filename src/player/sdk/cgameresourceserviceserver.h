#pragma once
#include "utils/addresses.h"
#include "utils/gameconfig.h"

class CGameEntitySystem;
extern CGameConfig* g_pGameConfig;

class CGameResourceService
{
public:
	CGameEntitySystem* GetGameEntitySystem()
	{
		// The offset comes from gamedata; without it (or before initialization)
		// the lookup must degrade to nullptr instead of dereferencing garbage.
		if (!g_pGameConfig)
		{
			return nullptr;
		}
		const int offset = g_pGameConfig->GetOffset("GameEntitySystem");
		if (offset < 0)
		{
			return nullptr;
		}
		return *reinterpret_cast<CGameEntitySystem**>((uintptr_t)(this) + offset);
	}
};
