#include "plugin.h"

#include "adapters/cs2/client_redirector.h"

#ifdef OW_ENABLE_CACHE_FALLBACK
#include "modules/persistence/cache_manager.h"
#include "modules/persistence/redis_cache.h"
#endif

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifdef OW_ENABLE_CACHE_FALLBACK
namespace
{
ow::persistence::RedisCacheConfig MakeRedisCacheConfig(const ow::core::ClusterConfig& config, std::string username,
													   std::string password)
{
	ow::persistence::RedisCacheConfig result;
	result.host = config.redisHost;
	result.port = config.redisPort;
	result.database = config.redisDatabase;
	result.username = std::move(username);
	result.password = std::move(password);
	result.keyPrefix = config.redisKeyPrefix;
	result.timeout = std::chrono::milliseconds(config.redisTimeoutMs);
	result.maxValueBytes = config.redisMaxValueBytes;
	return result;
}
} // namespace
#endif

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
	if (const auto* config = g_OpenWorld.GetClusterConfig())
	{
		META_CONPRINTF("[%s] Cluster server %s in environment %s.\n", PLUGIN_LOGTAG, config->serverId.c_str(),
					   config->environment.c_str());
		META_CONPRINTF("[%s] PostgreSQL: %s.\n", PLUGIN_LOGTAG,
					   g_OpenWorld.GetPersistenceAdapter() && g_OpenWorld.GetPersistenceAdapter()->IsConnected() ? "connected"
																												: "disconnected");
		META_CONPRINTF("[%s] Player settings cache: process-local read-through, 30-second TTL; authoritative data stays in PostgreSQL.\n",
					   PLUGIN_LOGTAG);
		std::size_t settingsLoading = 0;
		std::size_t settingsReady = 0;
		std::size_t settingsFailed = 0;
		for (int slot = 0; slot < 64; ++slot)
		{
			const auto* player = g_OpenWorld.FindPlayer(CPlayerSlot(slot));
			if (!player)
			{
				continue;
			}
			settingsLoading += player->settingsLoadStatus == PlayerSettingsLoadStatus::Loading;
			settingsReady += player->settingsLoadStatus == PlayerSettingsLoadStatus::Ready;
			settingsFailed += player->settingsLoadStatus == PlayerSettingsLoadStatus::Failed;
		}
		META_CONPRINTF("[%s] Player settings loads: %zu loading, %zu ready, %zu failed.\n", PLUGIN_LOGTAG,
					   settingsLoading, settingsReady, settingsFailed);
#ifdef OW_ENABLE_CACHE_FALLBACK
		META_CONPRINTF("[%s] Emergency cache fallback: compiled; configured provider %s.\n", PLUGIN_LOGTAG,
					   config->cacheProvider.c_str());
#else
		META_CONPRINTF("[%s] Cache fallback: disabled and not included in this build.\n", PLUGIN_LOGTAG);
#endif
	}
}

std::optional<std::string> ReadEnvironmentValue(const std::string& name)
{
	if (name.empty())
	{
		return std::string{};
	}
	const char* value = std::getenv(name.c_str());
	return value ? std::optional<std::string>(value) : std::nullopt;
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

	const char* configPathEnvironment = std::getenv("OW_CONFIG_PATH");
	const std::string configPath =
		configPathEnvironment && configPathEnvironment[0] != '\0'
			? configPathEnvironment
			: "addons/cs2-openworld/configs/openworld.ini";
	std::string configError;
	clusterConfig = ow::core::ClusterConfig::Load(configPath, &configError);
	if (!clusterConfig)
	{
		SetError(error, maxlen, configError.c_str());
		return false;
	}
	const auto connectionInfo = ReadEnvironmentValue(clusterConfig->persistenceConnectionEnv);
	if (!connectionInfo || connectionInfo->empty())
	{
		clusterConfig.reset();
		SetError(error, maxlen, "PostgreSQL connection environment variable is not set.");
		return false;
	}
	persistenceAdapter = std::make_unique<ow::adapters::PostgreSQLAdapter>();
	if (!persistenceAdapter->Connect(*connectionInfo) || !persistenceAdapter->Ping())
	{
		const std::string databaseError = persistenceAdapter->GetLastError();
		persistenceAdapter.reset();
		clusterConfig.reset();
		SetError(error, maxlen, databaseError.c_str());
		return false;
	}
#ifdef OW_ENABLE_CACHE_FALLBACK
	if (clusterConfig->cacheProvider == "redis")
	{
		auto username = ReadEnvironmentValue(clusterConfig->redisUsernameEnv);
		auto password = ReadEnvironmentValue(clusterConfig->redisPasswordEnv);
		if (!username || !password ||
			(!clusterConfig->redisUsernameEnv.empty() && username->empty()) ||
			(!clusterConfig->redisPasswordEnv.empty() && password->empty()))
		{
			clusterConfig.reset();
			persistenceAdapter.reset();
			SetError(error, maxlen, "Redis credential environment variable is not set.");
			return false;
		}
		auto redisCache = std::make_unique<ow::persistence::RedisCache>(
			MakeRedisCacheConfig(*clusterConfig, std::move(*username), std::move(*password)));
		if (!redisCache->Ping())
		{
			const std::string redisError = redisCache->GetLastError();
			clusterConfig.reset();
			persistenceAdapter.reset();
			SetError(error, maxlen, redisError.c_str());
			return false;
		}
		cacheStore = std::move(redisCache);
	}
	else
	{
		cacheStore = std::make_unique<ow::persistence::MemoryCache>();
	}
	cacheWarmer = std::make_unique<ow::persistence::AsyncCacheWarmer>(
		*cacheStore, clusterConfig->cacheWarmupWorkers, clusterConfig->cacheWarmupQueueCapacity);
#else
	META_CONPRINTF("[%s] Cache fallback is disabled; cache settings in openworld.ini are ignored.\n", PLUGIN_LOGTAG);
#endif
	try
	{
		playerSettingsWorker = std::make_unique<ow::persistence::PlayerSettingsWorker>(
			[this](std::uint64_t steamId, std::string& loadError) {
				if (!persistenceAdapter)
				{
					loadError = "PostgreSQL adapter is unavailable.";
					return std::optional<ow::adapters::PlayerSettings>{};
				}
				auto settings = persistenceAdapter->LoadPlayerSettings(steamId);
				if (!settings)
				{
					loadError = persistenceAdapter->GetLastError();
				}
				return settings;
			});
	}
	catch (const std::exception& exception)
	{
		persistenceAdapter.reset();
		clusterConfig.reset();
		SetError(error, maxlen, exception.what());
		return false;
	}

	if (!moduleManager.StartAll())
	{
		playerSettingsWorker.reset();
#ifdef OW_ENABLE_CACHE_FALLBACK
		cacheWarmer.reset();
		cacheStore.reset();
#endif
		persistenceAdapter.reset();
		clusterConfig.reset();
		SetError(error, maxlen, "OW Core modules failed to start.");
		return false;
	}

	coreModule = std::make_unique<ow::modules::CoreModule>(serviceRegistry, eventBus);
	moduleManager.RegisterModule(*coreModule);
	characterManager = std::make_unique<ow::modules::CharacterManager>(serviceRegistry, eventBus, persistenceAdapter.get());
	moduleManager.RegisterModule(*characterManager);
	transferModule = std::make_unique<ow::modules::TransferModule>(serviceRegistry, eventBus);
	moduleManager.RegisterModule(*transferModule);
	if (!moduleManager.StartAll())
	{
		transferModule.reset();
		characterManager.reset();
		coreModule.reset();
		playerSettingsWorker.reset();
#ifdef OW_ENABLE_CACHE_FALLBACK
		cacheWarmer.reset();
		cacheStore.reset();
#endif
		persistenceAdapter.reset();
		clusterConfig.reset();
		SetError(error, maxlen, "OW Core modules failed to start.");
		return false;
	}

	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);
	ismm->AddListener(this, this);
	loaded = true;
	InstallHooks();
	InstallGameEvents();

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
	RemoveGameEvents();
	eventBus = {};
	serviceRegistry = {};
	transferModule.reset();
	characterManager.reset();
	coreModule.reset();
	players.clear();
	players.clear();
	playerSettingsWorker.reset();
#ifdef OW_ENABLE_CACHE_FALLBACK
	cacheWarmer.reset();
	cacheStore.reset();
#endif
	persistenceAdapter.reset();
	clusterConfig.reset();
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
	for (auto& entry : players)
	{
		entry.second.steamAuthenticated = false;
		entry.second.readyForGameplay = false;
		ResetPlayerSettings(entry.second);
	}
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
	if (!loaded || paused)
	{
		return;
	}

	ProcessPlayerSettingsResults();
	if (!simulating)
	{
		return;
	}
	for (auto& entry : players)
	{
		if (!entry.second.steamAuthenticated && entry.second.inServer && entry.second.steamId != 0)
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
	player.readyForGameplay = false;
	ResetPlayerSettings(player);
}

void OpenWorldPlugin::OnClientActive(CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid)
{
	(void)loadGame;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client is active: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	auto& player = players[slot.Get()];
	if (player.steamId != 0 && xuid != 0 && player.steamId != xuid)
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerSettings(player);
	}
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
	if (loaded && !paused)
	{
		HandleTransferChatCommand(slot, command);
	}
}

void OpenWorldPlugin::HandleTransferChatCommand(CPlayerSlot slot, const CCommand& command)
{
	if (!engine || !clusterConfig || command.ArgC() < 2)
	{
		return;
	}

	const std::string_view commandName(command.Arg(0));
	if (commandName != "say" && commandName != "say_team")
	{
		return;
	}

	std::string chatText;
	for (int index = 1; index < command.ArgC(); ++index)
	{
		if (!chatText.empty())
		{
			chatText.push_back(' ');
		}
		chatText += command.Arg(index);
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string destinationId;
	std::string extra;
	message >> verb;
	if (verb != "!go" && verb != "!transfer")
	{
		return;
	}
	if (!(message >> destinationId) || (message >> extra))
	{
		engine->ClientPrintf(slot, "Usage: !go <server_id>\n");
		return;
	}

	const PlayerInfo* player = FindPlayer(slot);
	const CSteamID* steamId = engine->GetClientSteamID(slot);
	if (!player || !player->inServer || !player->fullyConnected || !player->steamAuthenticated ||
		!steamId || !steamId->IsValid() || steamId->ConvertToUint64() != player->steamId ||
		!engine->IsClientFullyAuthenticated(slot))
	{
		engine->ClientPrintf(slot, "Transfer requires a fully Steam-authenticated connection.\n");
		return;
	}

	const auto* destination = clusterConfig->FindServer(destinationId);
	if (!destination || destination->id == clusterConfig->serverId)
	{
		engine->ClientPrintf(slot, "Unknown destination server.\n");
		return;
	}

	ow::adapters::ClientRedirector redirector(engine);
	if (!redirector.Redirect(slot, destination->connect))
	{
		META_CONPRINTF("[%s] Could not request transfer for slot %d to server %s.\n", PLUGIN_LOGTAG, slot.Get(),
					   destination->id.c_str());
		engine->ClientPrintf(slot, "Could not start transfer. Please contact an administrator.\n");
		return;
	}

	META_CONPRINTF("[%s] Requested reconnect redirect for slot %d to server %s (%s).\n", PLUGIN_LOGTAG, slot.Get(),
				   destination->id.c_str(), destination->connect.c_str());
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
	const CPlayerSlot slot(player.slot);
	if (!engine || !player.inServer || !slot.IsValid())
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerSettings(player);
		return;
	}

	if (!engine->IsClientFullyAuthenticated(slot))
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerSettings(player);
		return;
	}

	if (player.steamId == 0)
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerSettings(player);
		return;
	}

	if (!player.steamAuthenticated)
	{
		META_CONPRINTF("[%s] Steam authorization confirmed for player slot %d.\n", PLUGIN_LOGTAG, player.slot);
	}
	player.steamAuthenticated = true;
	player.readyForGameplay = player.active && player.fullyConnected;
	if (player.active && player.fullyConnected &&
		player.settingsLoadStatus == PlayerSettingsLoadStatus::NotRequested)
	{
		RequestPlayerSettings(player);
	}
}

void OpenWorldPlugin::ResetPlayerSettings(PlayerInfo& player)
{
	player.settingsRequestId = 0;
	player.settingsLoadStatus = PlayerSettingsLoadStatus::NotRequested;
	player.settings.reset();
	player.settingsLoadError.clear();
}

void OpenWorldPlugin::RequestPlayerSettings(PlayerInfo& player)
{
	if (!playerSettingsWorker || player.steamId == 0)
	{
		player.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
		player.settingsLoadError = "Settings loading service or authenticated Steam ID is unavailable.";
		return;
	}
	++nextSettingsRequestId;
	if (nextSettingsRequestId == 0)
	{
		++nextSettingsRequestId;
	}
	player.settingsRequestId = nextSettingsRequestId;
	player.settingsLoadStatus = PlayerSettingsLoadStatus::Loading;
	player.settingsLoadError.clear();
	if (!playerSettingsWorker->Request(player.settingsRequestId, player.steamId))
	{
		player.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
		player.settingsLoadError = "Settings load queue is full or shutting down.";
		META_CONPRINTF("[%s] Could not queue player settings load for slot %d: %s\n", PLUGIN_LOGTAG, player.slot,
					   player.settingsLoadError.c_str());
	}
}

void OpenWorldPlugin::ProcessPlayerSettingsResults()
{
	if (!playerSettingsWorker)
	{
		return;
	}
	while (auto result = playerSettingsWorker->TryTakeResult())
	{
		const auto found = std::find_if(players.begin(), players.end(), [&result](const auto& entry) {
			return entry.second.settingsRequestId == result->requestId &&
				   entry.second.steamId == result->steamId;
		});
		if (found == players.end() || !found->second.inServer || !found->second.steamAuthenticated ||
			found->second.settingsLoadStatus != PlayerSettingsLoadStatus::Loading)
		{
			continue;
		}
		if (!result->settings)
		{
			found->second.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
			found->second.settingsLoadError = result->error;
			META_CONPRINTF("[%s] Player settings load failed for slot %d: %s\n", PLUGIN_LOGTAG,
						   found->second.slot, result->error.c_str());
			continue;
		}
		found->second.settings = std::move(result->settings);
		found->second.settingsLoadStatus = PlayerSettingsLoadStatus::Ready;
		found->second.settingsLoadError.clear();
		META_CONPRINTF("[%s] Player settings loaded for slot %d (revision %llu).\n", PLUGIN_LOGTAG,
					   found->second.slot,
					   static_cast<unsigned long long>(found->second.settings->revision));
	}
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

void OpenWorldPlugin::InstallGameEvents()
{
	if (gameEventsRegistered)
	{
		return;
	}
	gameEventManager = nullptr;
	if (!gameEventManager)
	{
		return;
	}
	if (gameEventManager->AddListener(this, "player_spawn", true))
	{
		gameEventsRegistered = true;
	}
}

void OpenWorldPlugin::RemoveGameEvents()
{
	if (gameEventManager && gameEventsRegistered)
	{
		gameEventManager->RemoveListener(this);
		gameEventsRegistered = false;
	}
	gameEventManager = nullptr;
}

void OpenWorldPlugin::FireGameEvent(IGameEvent* event)
{
	if (!event || !loaded || paused)
	{
		return;
	}
	const char* name = event->GetName();
	if (name && std::string_view(name) == "player_spawn")
	{
		HandlePlayerSpawned(name);
	}
}

void OpenWorldPlugin::HandlePlayerSpawned(const char* eventName)
{
	(void)eventName;
}

OpenWorldPlugin::PlayerInfo* OpenWorldPlugin::FindPlayerMutable(CPlayerSlot slot)
{
	auto player = players.find(slot.Get());
	return player == players.end() ? nullptr : &player->second;
}
