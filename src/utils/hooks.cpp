#include "hooks.h"

#include "cs2ac.h"
#include "movement_analysis/events/movement_events.h"
#include "movement_analysis/player_context.h"
#include "utils/ctimer.h"
#include "utils/gameconfig.h"
#include "utils/interfaces.h"
#include "utils/utils.h"

#include <khook.hpp>
#include "cs_gameevents.pb.h"
#include "igameevents.h"
#include "iserver.h"

namespace
{
	using TeleportHook = KHook::Virtual<CCSPlayerPawn, void, const Vector*, const QAngle*, const Vector*>;
	using PostEventAbstract = void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal*,
														 const CNetMessage*, unsigned long, NetChannelBufType_t);
	TeleportHook teleportHook;
	CCSPlayerPawn* teleportPawns[MAXPLAYERS] {};

	struct PendingGameEvent
	{
		IGameEvent* event;
		MovementPlayer* player;
	};

	std::vector<PendingGameEvent> pendingGameEvents;
	bool AddTeleportHook(MovementPlayer* player);

	bool IsConsumedEvent(IGameEvent* event)
	{
		if (!event)
		{
			return false;
		}
		const char* name = event->GetName();
		return CS2AC_STREQ(name, "weapon_fire") || CS2AC_STREQ(name, "player_hurt") || CS2AC_STREQ(name, "player_death")
			   || CS2AC_STREQ(name, "player_spawn") || CS2AC_STREQ(name, "smokegrenade_detonate");
	}

	MovementPlayer* ResolveEventPlayer(IGameEvent* event)
	{
		if (!event)
		{
			return nullptr;
		}
		auto* player = g_pCS2ACPlayerManager->ToPlayer(static_cast<CBasePlayerController*>(event->GetPlayerController("userid")));
		if (player)
		{
			return player;
		}

		int userID = event->GetInt("userid", -1);
		return userID < 0 ? nullptr : g_pCS2ACPlayerManager->ToPlayer(CPlayerUserId(userID));
	}

	// Ignore lets the engine call the original and preserves its return value.
	KHook::Return<bool> HookFireEventBefore(IGameEventManager2*, IGameEvent* event, bool)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore, {}};
		}
		PendingGameEvent pending {};
		if (IsConsumedEvent(event))
		{
			pending = {interfaces::pGameEventManager->DuplicateEvent(event), ResolveEventPlayer(event)};
		}
		pendingGameEvents.push_back(pending);
		return {KHook::Action::Ignore, {}};
	}

	KHook::Return<void> HookPostEvent(IGameEventSystem*, CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal* event,
									 const CNetMessage* data, unsigned long, NetChannelBufType_t)
	{
		if (!g_CS2AC.IsLoaded() || !event || !data)
		{
			return {KHook::Action::Ignore};
		}
		auto* info = event->GetNetMessageInfo();
		if (!info)
		{
			return {KHook::Action::Ignore};
		}
		if (info->m_MessageId == GE_FireBulletsId)
		{
			g_CS2AC.OnFireBullets(*data->ToPB<CMsgTEFireBullets>());
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<bool> HookFireEventAfter(IGameEventManager2*, IGameEvent*, bool)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore, {}};
		}
		PendingGameEvent pending {};
		if (!pendingGameEvents.empty())
		{
			pending = pendingGameEvents.back();
			pendingGameEvents.pop_back();
		}
		if (pending.event)
		{
			g_CS2AC.OnGameEvent(pending.event, pending.player);
			if (CS2AC_STREQ(pending.event->GetName(), "player_spawn") && pending.player)
			{
				// A spawn can replace the pawn object after ClientActive, so bind the per-pawn hook again.
				pending.player->OnTeleport(nullptr, nullptr, nullptr);
				AddTeleportHook(pending.player);
			}
			interfaces::pGameEventManager->FreeEvent(pending.event);
		}
		return {KHook::Action::Ignore, {}};
	}

	KHook::Return<void> HookTeleport(CCSPlayerPawn* pawn, const Vector* origin, const QAngle* angles, const Vector* velocity)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		auto* current = g_pCS2ACPlayerManager->ToPlayer(static_cast<CBasePlayerPawn*>(pawn));
		if (current)
		{
			current->OnTeleport(origin, angles, velocity);
		}
		return {KHook::Action::Ignore};
	}

	bool RemoveTeleportHook(CPlayerSlot slot)
	{
		if (slot.Get() < 0 || slot.Get() >= MAXPLAYERS || !teleportPawns[slot.Get()])
		{
			return true;
		}
		teleportHook.Remove(teleportPawns[slot.Get()]);
		teleportPawns[slot.Get()] = nullptr;
		return true;
	}

	bool AddTeleportHook(MovementPlayer* player)
	{
		if (!player || !player->GetPlayerPawn())
		{
			return false;
		}
		const i32 slot = player->GetPlayerSlot().Get();
		if (slot < 0 || slot >= MAXPLAYERS || !RemoveTeleportHook(player->GetPlayerSlot()))
		{
			return false;
		}
		auto* pawn = static_cast<CCSPlayerPawn*>(player->GetPlayerPawn());
		teleportHook.Add(pawn);
		teleportPawns[slot] = pawn;
		return true;
	}

	KHook::Return<void> HookGameFrameBefore(ISource2Server*, bool, bool, bool)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		if (auto* globals = g_pCS2ACUtils->GetGlobals())
		{
			g_CS2AC.serverGlobals = *globals;
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookGameFrameAfter(ISource2Server*, bool simulating, bool, bool)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		if (auto* globals = g_pCS2ACUtils->GetGlobals())
		{
			g_CS2AC.serverGlobals = *globals;
		}
		g_CS2AC.OnGameFrame(simulating);
		ProcessTimers();
		MovementEventService::ActiveCheck();
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientFullyConnect(ISource2GameClients*, CPlayerSlot slot)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_pCS2ACPlayerManager->OnClientFullyConnect(slot);
		g_ClientCvarValue.OnClientFullyConnected(slot, g_pCS2ACPlayerManager->ToPlayer(slot)->IsFakeClient());
		g_CS2AC.OnClientFullyConnect(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientSettingsChanged(ISource2GameClients*, CPlayerSlot slot)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_CS2AC.OnClientSettingsChanged(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientActive(ISource2GameClients*, CPlayerSlot slot, bool, const char*, uint64 xuid)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_pCS2ACPlayerManager->OnClientActive(slot, xuid);
		auto* player = g_pCS2ACPlayerManager->ToPlayer(slot);
		if (player && player->GetPlayerPawn())
		{
			AddTeleportHook(player);
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientDisconnect(ISource2GameClients*, CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64,
											 const char*)
	{
		if (!g_CS2AC.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		RemoveTeleportHook(slot);
		g_ClientCvarValue.OnClientDisconnect(slot);
		g_CS2AC.OnClientDisconnect(slot);
		g_pCS2ACPlayerManager->OnClientDisconnect(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Virtual<ISource2Server, void, bool, bool, bool> gameFrameHook{
		&ISource2Server::GameFrame, &HookGameFrameBefore, &HookGameFrameAfter};
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientFullyConnectHook{
		&ISource2GameClients::ClientFullyConnect, &HookClientFullyConnect, nullptr};
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientSettingsChangedHook{
		&ISource2GameClients::ClientSettingsChanged, &HookClientSettingsChanged, nullptr};
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot, bool, const char*, uint64> clientActiveHook{
		&ISource2GameClients::ClientActive, &HookClientActive, nullptr};
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char*, uint64, const char*>
		clientDisconnectHook{&ISource2GameClients::ClientDisconnect, &HookClientDisconnect, nullptr};
	KHook::Virtual<IGameEventManager2, bool, IGameEvent*, bool> fireEventHook{
		&IGameEventManager2::FireEvent, &HookFireEventBefore, &HookFireEventAfter};
	KHook::Virtual<IGameEventSystem, void, CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal*, const CNetMessage*,
				   unsigned long, NetChannelBufType_t>
		postEventHook{&HookPostEvent, nullptr};
} // namespace

bool hooks::Initialize(std::vector<std::string>& missing)
{
	if (!KHook::__exported__khook)
	{
		missing.emplace_back("Metamod's detour service is unavailable.");
		return false;
	}
	const i32 teleportOffset = g_pGameConfig ? g_pGameConfig->GetOffset("Teleport") : -1;
	if (teleportOffset < 0)
	{
		missing.emplace_back("The player teleport offset is unavailable.");
		return false;
	}
	if (!interfaces::pServer || !g_pSource2GameClients || !interfaces::pGameEventManager || !interfaces::pGameEventSystem)
	{
		missing.emplace_back("One or more server hook interfaces are unavailable.");
		return false;
	}
	teleportHook.Configure(teleportOffset);
	postEventHook.Configure(static_cast<PostEventAbstract>(&IGameEventSystem::PostEventAbstract));
	gameFrameHook.Add(interfaces::pServer);
	clientFullyConnectHook.Add(g_pSource2GameClients);
	clientSettingsChangedHook.Add(g_pSource2GameClients);
	clientActiveHook.Add(g_pSource2GameClients);
	clientDisconnectHook.Add(g_pSource2GameClients);
	fireEventHook.Add(interfaces::pGameEventManager);
	postEventHook.Add(interfaces::pGameEventSystem);
	return true;
}

void hooks::HookActivePlayers()
{
	for (i32 i = 1; i <= MAXPLAYERS; ++i)
	{
		auto* player = g_pCS2ACPlayerManager->ToPlayer(static_cast<u32>(i));
		if (player && player->IsInGame() && player->GetPlayerPawn())
		{
			AddTeleportHook(player);
		}
	}
}

bool hooks::ResetMap()
{
	bool removed = true;
	for (i32 slot = 0; slot < MAXPLAYERS; ++slot)
	{
		removed = RemoveTeleportHook(CPlayerSlot(slot)) && removed;
	}
	return removed;
}

bool hooks::Cleanup()
{
	bool removed = ResetMap();
	gameFrameHook.Remove(interfaces::pServer);
	clientFullyConnectHook.Remove(g_pSource2GameClients);
	clientSettingsChangedHook.Remove(g_pSource2GameClients);
	clientActiveHook.Remove(g_pSource2GameClients);
	clientDisconnectHook.Remove(g_pSource2GameClients);
	fireEventHook.Remove(interfaces::pGameEventManager);
	postEventHook.Remove(interfaces::pGameEventSystem);
	if (interfaces::pGameEventManager)
	{
		for (const auto& pending : pendingGameEvents)
		{
			if (pending.event)
			{
				interfaces::pGameEventManager->FreeEvent(pending.event);
			}
		}
	}
	pendingGameEvents.clear();
	return removed;
}
