#include "plugin.h"

#include <cstdio>

namespace
{
CConVar<int> ow_core_enabled("ow_core_enabled", FCVAR_NONE, "Enable or disable OW Core gameplay processing.", 1, true, 0.0f, true,
							 1.0f);

void SetError(char* error, size_t maxlen, const char* message)
{
	if (error && maxlen > 0)
	{
		std::snprintf(error, maxlen, "%s", message);
	}
}

KHook::Return<void> HookGameFrame(ISource2Server*, bool simulating, bool, bool)
{
	g_OpenWorld.OnGameFrame(simulating);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientPutInServer(ISource2GameClients*, CPlayerSlot slot, const char* name, int type, uint64 xuid)
{
	g_OpenWorld.OnClientPutInServer(slot, name, type, xuid);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientActive(ISource2GameClients*, CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid)
{
	g_OpenWorld.OnClientActive(slot, loadGame, name, xuid);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientFullyConnect(ISource2GameClients*, CPlayerSlot slot)
{
	g_OpenWorld.OnClientFullyConnect(slot);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientDisconnect(ISource2GameClients*, CPlayerSlot slot, ENetworkDisconnectionReason, const char* name, uint64 xuid,
										 const char*)
{
	g_OpenWorld.OnClientDisconnect(slot, name, xuid);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientCommand(ISource2GameClients*, CPlayerSlot slot, const CCommand& command)
{
	g_OpenWorld.OnClientCommand(slot, command);
	return {KHook::Action::Ignore};
}

KHook::Return<void> HookClientSettingsChanged(ISource2GameClients*, CPlayerSlot slot)
{
	g_OpenWorld.OnClientSettingsChanged(slot);
	return {KHook::Action::Ignore};
}

KHook::Virtual<ISource2Server, void, bool, bool, bool> gameFrameHook{&ISource2Server::GameFrame, nullptr, &HookGameFrame};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot, char const*, int, uint64> clientPutInServerHook{
	&ISource2GameClients::ClientPutInServer, nullptr, &HookClientPutInServer};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot, bool, const char*, uint64> clientActiveHook{
	&ISource2GameClients::ClientActive, nullptr, &HookClientActive};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientFullyConnectHook{&ISource2GameClients::ClientFullyConnect, nullptr,
																				&HookClientFullyConnect};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char*, uint64, const char*>
	clientDisconnectHook{&ISource2GameClients::ClientDisconnect, nullptr, &HookClientDisconnect};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot, const CCommand&> clientCommandHook{&ISource2GameClients::ClientCommand, nullptr,
																						 &HookClientCommand};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientSettingsChangedHook{&ISource2GameClients::ClientSettingsChanged, nullptr,
																				  &HookClientSettingsChanged};
} // namespace

OpenWorldPlugin g_OpenWorld;

PLUGIN_EXPOSE(OpenWorldPlugin, g_OpenWorld);

CON_COMMAND_F(ow_status, "Show OW Core status.", FCVAR_NONE)
{
	(void)context;
	(void)args;
	META_CONPRINTF("[%s] %s; OW Core gameplay processing %s.\n", PLUGIN_LOGTAG, g_OpenWorld.IsLoaded() ? "running" : "not running",
				   ow_core_enabled.Get() ? "enabled" : "disabled");
}

bool OpenWorldPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	(void)late;
	PLUGIN_SAVEVARS();

	if (!ismm)
	{
		SetError(error, maxlen, "Metamod API is unavailable.");
		return false;
	}

	server = static_cast<ISource2Server*>(ismm->VInterfaceMatch(ismm->GetServerFactory(), INTERFACEVERSION_SERVERGAMEDLL));
	engine = static_cast<IVEngineServer2*>(ismm->VInterfaceMatch(ismm->GetEngineFactory(), INTERFACEVERSION_VENGINESERVER));
	gameClients = static_cast<ISource2GameClients*>(ismm->VInterfaceMatch(ismm->GetServerFactory(), INTERFACEVERSION_SERVERGAMECLIENTS));
	if (!engine)
	{
		SetError(error, maxlen, "The engine interface required for Steam authorization is unavailable.");
		return false;
	}
	if (!moduleManager.StartAll())
	{
		SetError(error, maxlen, "OW Core modules failed to start.");
		return false;
	}

	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);
	ismm->AddListener(this, this);
	loaded = true;
	InstallHooks();

	META_CONPRINTF("[%s] %s %s loaded.\n", PLUGIN_LOGTAG, PLUGIN_DISPLAY_NAME, PLUGIN_FULL_VERSION);
	return true;
}

void OpenWorldPlugin::AllPluginsLoaded()
{
	if (loaded)
	{
		InstallHooks();
		META_CONPRINTF("[%s] All Metamod plugins are loaded.\n", PLUGIN_LOGTAG);
	}
}

bool OpenWorldPlugin::QueryRunning(char* error, size_t maxlen)
{
	if (loaded)
	{
		return true;
	}

	SetError(error, maxlen, "The plugin has not completed loading.");
	return false;
}

bool OpenWorldPlugin::Unload(char* error, size_t maxlen)
{
	(void)error;
	(void)maxlen;

	RemoveHooks();
	moduleManager.StopAll();
	loaded = false;
	paused = false;
	players.clear();
	server = nullptr;
	engine = nullptr;
	gameClients = nullptr;
	return true;
}

bool OpenWorldPlugin::Pause(char* error, size_t maxlen)
{
	(void)error;
	(void)maxlen;

	paused = true;
	RemoveHooks();
	moduleManager.StopAll();
	return true;
}

bool OpenWorldPlugin::Unpause(char* error, size_t maxlen)
{
	if (!moduleManager.StartAll())
	{
		SetError(error, maxlen, "OW Core modules failed to restart.");
		return false;
	}
	paused = false;
	InstallHooks();
	return true;
}

void OpenWorldPlugin::OnLevelInit(char const* mapName, char const* mapEntities, char const* oldLevel, char const* landmarkName, bool loadGame,
						   bool background)
{
	(void)mapEntities;
	(void)oldLevel;
	(void)landmarkName;
	(void)loadGame;
	(void)background;

	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Map started: %s.\n", PLUGIN_LOGTAG, mapName ? mapName : "<unknown>");
	}
}

void OpenWorldPlugin::OnLevelShutdown()
{
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Map is shutting down.\n", PLUGIN_LOGTAG);
	}
}

void OpenWorldPlugin::OnGameFrame(bool simulating)
{
	if (!loaded || paused || !simulating)
	{
		return;
	}

	for (auto& entry : players)
	{
		if (!entry.second.steamAuthenticated)
		{
			RefreshSteamAuthorization(entry.second);
		}
	}

}

void OpenWorldPlugin::OnClientPutInServer(CPlayerSlot slot, const char* name, int type, uint64 xuid)
{
	(void)type;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client entered the server: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	auto& player = players[slot.Get()];
	player.slot = slot.Get();
	player.name = name ? name : "";
	player.steamId = xuid;
	player.inServer = true;
	player.active = false;
	player.fullyConnected = false;
	player.steamAuthenticated = false;
}

void OpenWorldPlugin::OnClientActive(CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid)
{
	(void)loadGame;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client is active: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	auto& player = players[slot.Get()];
	player.slot = slot.Get();
	player.name = name ? name : "";
	player.steamId = xuid;
	player.active = true;
	RefreshSteamAuthorization(player);
}

void OpenWorldPlugin::OnClientFullyConnect(CPlayerSlot slot)
{
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client fully connected: slot %d.\n", PLUGIN_LOGTAG, slot.Get());
	}
	auto& player = players[slot.Get()];
	player.slot = slot.Get();
	player.fullyConnected = true;
	RefreshSteamAuthorization(player);
}

void OpenWorldPlugin::OnClientDisconnect(CPlayerSlot slot, const char* name, uint64 xuid)
{
	(void)xuid;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client disconnected: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	players.erase(slot.Get());
}

void OpenWorldPlugin::OnClientCommand(CPlayerSlot slot, const CCommand& command)
{
	(void)slot;
	(void)command;
}

void OpenWorldPlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
	(void)slot;
}

const OpenWorldPlugin::PlayerInfo* OpenWorldPlugin::FindPlayer(CPlayerSlot slot) const
{
	const auto player = players.find(slot.Get());
	return player == players.end() ? nullptr : &player->second;
}

void OpenWorldPlugin::RefreshSteamAuthorization(PlayerInfo& player)
{
	if (!engine)
	{
		player.steamAuthenticated = false;
		return;
	}

	const CPlayerSlot slot(player.slot);
	const CSteamID* steamId = engine->GetClientSteamID(slot);
	const bool authenticated = steamId && steamId->IsValid() && engine->IsClientFullyAuthenticated(slot);
	if (authenticated && !player.steamAuthenticated)
	{
		player.steamId = steamId->ConvertToUint64();
		META_CONPRINTF("[%s] Steam authorization confirmed for player slot %d.\n", PLUGIN_LOGTAG, player.slot);
	}
	player.steamAuthenticated = authenticated;
	player.readyForGameplay = player.steamAuthenticated && player.active && player.fullyConnected;
}

void OpenWorldPlugin::InstallHooks()
{
	if (!KHook::__exported__khook)
	{
		META_CONPRINTF("[%s] Metamod's hook service is unavailable; gameplay hooks are disabled.\n", PLUGIN_LOGTAG);
		return;
	}

	if (g_SMAPI && !server)
	{
		server = static_cast<ISource2Server*>(g_SMAPI->VInterfaceMatch(g_SMAPI->GetServerFactory(), INTERFACEVERSION_SERVERGAMEDLL));
	}
	if (g_SMAPI && !gameClients)
	{
		gameClients = static_cast<ISource2GameClients*>(
			g_SMAPI->VInterfaceMatch(g_SMAPI->GetServerFactory(), INTERFACEVERSION_SERVERGAMECLIENTS));
	}

	if (server && !serverHookInstalled)
	{
		gameFrameHook.Add(server);
		serverHookInstalled = true;
	}
	if (gameClients && !clientHooksInstalled)
	{
		clientPutInServerHook.Add(gameClients);
		clientActiveHook.Add(gameClients);
		clientFullyConnectHook.Add(gameClients);
		clientDisconnectHook.Add(gameClients);
		clientCommandHook.Add(gameClients);
		clientSettingsChangedHook.Add(gameClients);
		clientHooksInstalled = true;
	}

	if (!serverHookInstalled && !clientHooksInstalled)
	{
		META_CONPRINTF("[%s] Server hook interfaces are unavailable; lifecycle callbacks remain active.\n", PLUGIN_LOGTAG);
	}
}

void OpenWorldPlugin::RemoveHooks()
{
	if (server && serverHookInstalled)
	{
		gameFrameHook.Remove(server);
		serverHookInstalled = false;
	}
	if (gameClients && clientHooksInstalled)
	{
		clientPutInServerHook.Remove(gameClients);
		clientActiveHook.Remove(gameClients);
		clientFullyConnectHook.Remove(gameClients);
		clientDisconnectHook.Remove(gameClients);
		clientCommandHook.Remove(gameClients);
		clientSettingsChangedHook.Remove(gameClients);
		clientHooksInstalled = false;
	}
}
