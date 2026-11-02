#include "plugin.h"

#include "adapters/cs2/client_redirector.h"
#include "player/player.h"
#include "ui/particle/particle_menu_manager.h"
#include "utils/addresses.h"
#include "utils/engine_server_compat.h"
#include "utils/utils.h"

using ow::ui::particle::ParticleMenuManager;

#ifdef OW_ENABLE_CACHE_FALLBACK
#include "modules/persistence/cache_manager.h"
#include "modules/persistence/redis_cache.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

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

bool BuildChatText(const CCommand& command, std::string& text)
{
	if (command.ArgC() < 2)
	{
		return false;
	}
	const std::string_view commandName(command.Arg(0));
	if (commandName != "say" && commandName != "say_team")
	{
		return false;
	}
	text.clear();
	for (int index = 1; index < command.ArgC(); ++index)
	{
		if (!text.empty())
		{
			text.push_back(' ');
		}
		text += command.Arg(index);
	}
	return !text.empty();
}

std::string TrimChatWhitespace(std::string value)
{
	const auto isSpace = [](unsigned char character) { return std::isspace(character) != 0; };
	value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
	value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
	return value;
}

bool IsSafeCharacterName(std::string_view name)
{
	return !name.empty() && name.size() <= 64 &&
		   std::all_of(name.begin(), name.end(), [](unsigned char character) {
			   return character >= 0x20 && character != 0x7f;
		   });
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
	const bool handled = g_OpenWorld.OnClientCommand(slot, command);
	return {handled ? KHook::Action::Supersede : KHook::Action::Ignore};
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
KHook::Virtual<ISource2GameClients, void, CPlayerSlot, const CCommand&> clientCommandHook{&ISource2GameClients::ClientCommand,
																						 &HookClientCommand, nullptr};
KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientSettingsChangedHook{&ISource2GameClients::ClientSettingsChanged, nullptr,
																				  &HookClientSettingsChanged};
} // namespace

OpenWorldPlugin::OpenWorldPlugin() : hudSystem(uiManager)
{
	inputManager.SetActionHandler([this](CPlayerSlot slot, ow::ui::InputAction action) {
		HandleInputAction(slot, action);
	});
	hudSystem.RegisterHUDElement({"server", "Server", "unknown", ow::ui::AllHUDProfiles});
	hudSystem.RegisterHUDElement({"session", "Session", "Connecting", ow::ui::AllHUDProfiles});
	hudSystem.RegisterHUDElement({"settings", "Settings", "Waiting", ow::ui::AllHUDProfiles});
	hudSystem.RegisterHUDElement({"character", "Character", "Not selected", ow::ui::AllHUDProfiles});
	hudSystem.RegisterHUDElement({"wallet", "Wallet", "Unavailable", ow::ui::AllHUDProfiles});
}

OpenWorldPlugin g_OpenWorld;

PLUGIN_EXPOSE(OpenWorldPlugin, g_OpenWorld);

CON_COMMAND_F(ow_status, "Show OW Core status.", FCVAR_NONE)
{
	(void)context;
	(void)args;
	const auto* config = g_OpenWorld.GetClusterConfig();
	const bool gameplayEnabled = ow_core_enabled.Get() != 0 && (!config || config->enabled);
	META_CONPRINTF("[%s] %s; OW Core gameplay processing %s.\n", PLUGIN_LOGTAG, g_OpenWorld.IsLoaded() ? "running" : "not running",
				   gameplayEnabled ? "enabled" : "disabled");
	if (config)
	{
		META_CONPRINTF("[%s] Cluster server %s in environment %s.\n", PLUGIN_LOGTAG, config->serverId.c_str(), config->environment.c_str());
		if (config->worldMode == ow::core::WorldMode::SingleMap)
		{
			META_CONPRINTF("[%s] World mode: single map (%s).\n", PLUGIN_LOGTAG, config->worldMap.c_str());
		}
		else
		{
			META_CONPRINTF("[%s] World mode: multi-server cluster.\n", PLUGIN_LOGTAG);
		}
		META_CONPRINTF("[%s] Adapters: PostgreSQL=%s, Panorama=%s, HUD=%s, input=%s, map overview=%s.\n", PLUGIN_LOGTAG,
					   config->adapters.postgresql ? "on" : "off", config->adapters.panorama ? "on" : "off", config->adapters.hud ? "on" : "off",
					   config->adapters.input ? "on" : "off", config->adapters.mapOverview ? "on" : "off");
		META_CONPRINTF("[%s] Modules: characters=%s, economy=%s, inventory=%s, transfer=%s.\n", PLUGIN_LOGTAG,
					   config->modules.characters ? "on" : "off", config->modules.economy ? "on" : "off", config->modules.inventory ? "on" : "off",
					   config->modules.transfer ? "on" : "off");
		const char* postgresqlStatus = !config->adapters.postgresql                                                                ? "disabled"
									   : g_OpenWorld.GetPersistenceAdapter() && g_OpenWorld.GetPersistenceAdapter()->IsConnected() ? "connected"
																																   : "disconnected";
		META_CONPRINTF("[%s] PostgreSQL: %s.\n", PLUGIN_LOGTAG, postgresqlStatus);
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
		META_CONPRINTF("[%s] Player settings loads: %zu loading, %zu ready, %zu failed.\n", PLUGIN_LOGTAG, settingsLoading, settingsReady,
					   settingsFailed);
#ifdef OW_ENABLE_CACHE_FALLBACK
		META_CONPRINTF("[%s] Emergency cache fallback: compiled; configured provider %s.\n", PLUGIN_LOGTAG, config->cacheProvider.c_str());
#else
		META_CONPRINTF("[%s] Cache fallback: disabled and not included in this build.\n", PLUGIN_LOGTAG);
#endif
	}
}

std::optional<std::string> ReadEnvironmentValue(const std::string& name)
{
	if (name.empty())
	{
		return std::string {};
	}
	const char* value = std::getenv(name.c_str());
	return value ? std::optional<std::string>(value) : std::nullopt;
}

bool OpenWorldPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	(void)late;
	PLUGIN_SAVEVARS();
	if (loaded)
	{
		SetError(error, maxlen, "The plugin is already loaded.");
		return false;
	}
	moduleManager.Clear();

	if (!ismm)
	{
		SetError(error, maxlen, "Metamod API is unavailable.");
		return false;
	}

	// Bootstrap the SDK runtime state (engine modules, interfaces, gamedata)
	// before anything below touches schema fields, signatures or offsets.
	// Failures are logged and tolerated so the rest of the plugin still runs
	// with reduced functionality instead of crashing on missing keys.
	std::vector<std::string> missing;
	modules::Initialize(missing);
	interfaces::Initialize(ismm, missing);
	utils::Initialize(missing);
	for (const std::string& item : missing)
	{
		META_CONPRINTF("[%s] Missing dependency: %s\n", PLUGIN_LOGTAG, item.c_str());
	}

	engine = static_cast<IVEngineServer2*>(
		ismm->VInterfaceMatch(ismm->GetEngineFactory(), INTERFACEVERSION_VENGINESERVER));
	if (!engine)
	{
		SetError(error, maxlen, "The CS2 engine server interface is unavailable.");
		return false;
	}

	const char* configPathEnvironment = std::getenv("OW_CONFIG_PATH");
	const std::filesystem::path configuredConfigPath =
		configPathEnvironment && configPathEnvironment[0] != '\0'
			? std::filesystem::path(configPathEnvironment)
			: std::filesystem::path("addons/cs2-openworld/configs/openworld.ini");
	CBufferStringGrowable<256> gameDirectory;
	engine->GetGameDir(gameDirectory);
	const std::filesystem::path gameDirectoryPath(gameDirectory.Get());
	if (gameDirectoryPath.empty())
	{
		SetError(error, maxlen, "The CS2 game directory is unavailable.");
		return false;
	}
	const std::filesystem::path configPath = configuredConfigPath.is_absolute()
												 ? configuredConfigPath
												 : gameDirectoryPath / configuredConfigPath;
	std::string configError;
	clusterConfig = ow::core::ClusterConfig::Load(configPath.string(), &configError);
	if (!clusterConfig)
	{
		SetError(error, maxlen, configError.c_str());
		return false;
	}
	if (clusterConfig->adapters.postgresql)
	{
		const auto connectionInfo = ReadEnvironmentValue(clusterConfig->persistenceConnectionEnv);
		if (!connectionInfo || connectionInfo->empty())
		{
			clusterConfig.reset();
			SetError(error, maxlen, "PostgreSQL connection environment variable is not set.");
			return false;
		}
		persistenceAdapter = std::make_unique<ow::adapters::PostgreSQLAdapter>();
		if (!persistenceAdapter->Connect(*connectionInfo, clusterConfig->persistenceConnectTimeoutSeconds) ||
			!persistenceAdapter->Ping())
		{
			const std::string databaseError = persistenceAdapter->GetLastError();
			persistenceAdapter.reset();
			clusterConfig.reset();
			SetError(error, maxlen, databaseError.c_str());
			return false;
		}
	}
	else
	{
		META_CONPRINTF("[%s] PostgreSQL adapter disabled by configuration.\n", PLUGIN_LOGTAG);
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
		auto redisCache =
			std::make_unique<ow::persistence::RedisCache>(MakeRedisCacheConfig(*clusterConfig, std::move(*username), std::move(*password)));
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
	cacheWarmer =
		std::make_unique<ow::persistence::AsyncCacheWarmer>(*cacheStore, clusterConfig->cacheWarmupWorkers, clusterConfig->cacheWarmupQueueCapacity);
#else
	META_CONPRINTF("[%s] Cache fallback is disabled; cache settings in openworld.ini are ignored.\n", PLUGIN_LOGTAG);
#endif
	if (clusterConfig->adapters.postgresql)
	{
		try
		{
			if (clusterConfig->routing.enforceServerAffinity)
			{
				playerRouteWorker = std::make_unique<ow::persistence::PlayerRouteWorker>(
					[this](const ow::persistence::PlayerRouteJob& job, ow::persistence::PlayerRouteResult& result)
					{
						if (!persistenceAdapter)
						{
							result.error = "PostgreSQL adapter is unavailable.";
							return;
						}
						std::optional<std::string> serverId;
						if (job.operation == ow::persistence::PlayerRouteOperation::Resolve)
						{
							serverId = persistenceAdapter->ResolvePlayerServerRoute(job.environment, job.steamId,
								job.currentServerId, job.lobbyServerId);
						}
						else
						{
							serverId = persistenceAdapter->AssignPlayerServerRoute(job.environment, job.steamId,
								job.destinationServerId);
						}
						if (!serverId)
						{
							result.error = persistenceAdapter->GetLastError();
							return;
						}
						result.serverId = std::move(*serverId);
					});
			}
			playerSettingsWorker = std::make_unique<ow::persistence::PlayerSettingsWorker>(
				[this](std::uint64_t steamId, std::string& loadError)
				{
					if (!persistenceAdapter)
					{
						loadError = "PostgreSQL adapter is unavailable.";
						return std::optional<ow::adapters::PlayerSettings> {};
					}
					auto settings = persistenceAdapter->LoadPlayerSettings(steamId);
					if (!settings)
					{
						loadError = persistenceAdapter->GetLastError();
					}
					return settings;
				});
			if (clusterConfig->modules.characters)
			{
				playerCharacterWorker = std::make_unique<ow::persistence::PlayerCharacterWorker>(
					[this](const ow::persistence::PlayerCharacterJob& job, ow::persistence::PlayerCharacterResult& result)
					{
						if (!persistenceAdapter)
						{
							result.error = "PostgreSQL adapter is unavailable.";
							return;
						}
						if (job.operation == ow::persistence::PlayerCharacterOperation::List)
						{
							auto characters = persistenceAdapter->ListCharacters(job.steamId);
							if (!characters)
							{
								result.error = persistenceAdapter->GetLastError();
								return;
							}
							result.characters = std::move(*characters);
							return;
						}

						std::optional<std::string> characterId;
						if (job.operation == ow::persistence::PlayerCharacterOperation::Create)
						{
							characterId = persistenceAdapter->CreateCharacter(job.steamId, job.value);
							if (!characterId)
							{
								result.error = persistenceAdapter->GetLastError();
								return;
							}
						}
						else
						{
							characterId = job.value;
						}

						auto character = persistenceAdapter->LoadCharacterForPlayer(job.steamId, *characterId);
						if (!character)
						{
							result.error = persistenceAdapter->GetLastError();
							return;
						}
						if (inventorySystem)
						{
							auto inventory = inventorySystem->Load(*characterId);
							if (!inventory)
							{
								result.error = inventorySystem->GetLastError();
								return;
							}
							character->inventoryJson = std::move(inventory->itemsJson);
							character->inventoryRevision = inventory->revision;
						}
						else
						{
							character->inventoryJson = "[]";
						}
						if (economySystem)
						{
							const auto wallet = economySystem->LoadWallet(*characterId);
							if (!wallet)
							{
								result.error = economySystem->GetLastError();
								return;
							}
							character->balance = wallet->balance;
							character->balanceRevision = wallet->revision;
						}
						result.character = std::move(character);
					});
			}
		}
		catch (const std::exception& exception)
		{
			playerRouteWorker.reset();
			playerCharacterWorker.reset();
			playerSettingsWorker.reset();
#ifdef OW_ENABLE_CACHE_FALLBACK
			cacheWarmer.reset();
			cacheStore.reset();
#endif
			persistenceAdapter.reset();
			clusterConfig.reset();
			SetError(error, maxlen, exception.what());
			return false;
		}
	}

	bool uiServicesReady = serviceRegistry.Register<ow::ui::UIManager>(uiManager, 1);
	if (clusterConfig->adapters.panorama)
	{
		if (panoramaAdapter.Initialize(ismm))
		{
			uiServicesReady =
				uiManager.RegisterAdapter(panoramaAdapter) && serviceRegistry.Register<ow::ui::IUIAdapter>(panoramaAdapter, 1) && uiServicesReady;
		}
		else
		{
			META_CONPRINTF("[%s] Panorama text adapter is unavailable; using engine text fallback.\n", PLUGIN_LOGTAG);
		}
	}
	if (clusterConfig->adapters.hud)
	{
		uiServicesReady = serviceRegistry.Register<ow::ui::HUDSystem>(hudSystem, 1) && uiServicesReady;
	}
	if (clusterConfig->adapters.input)
	{
		uiServicesReady = serviceRegistry.Register<ow::ui::InputManager>(inputManager, 1) && uiServicesReady;
	}
	if (!uiServicesReady)
	{
		serviceRegistry = {};
		uiManager.UnregisterAdapter(panoramaAdapter);
		panoramaAdapter.Shutdown();
		playerRouteWorker.reset();
		playerCharacterWorker.reset();
		playerSettingsWorker.reset();
#ifdef OW_ENABLE_CACHE_FALLBACK
		cacheWarmer.reset();
		cacheStore.reset();
#endif
		persistenceAdapter.reset();
		clusterConfig.reset();
		SetError(error, maxlen, "UI services could not be registered.");
		return false;
	}

	bool modulesRegistered = true;
	coreModule = std::make_unique<ow::modules::CoreModule>(serviceRegistry, eventBus);
	modulesRegistered = moduleManager.RegisterModule(*coreModule) && modulesRegistered;
	if (clusterConfig->modules.characters)
	{
		characterManager = std::make_unique<ow::modules::CharacterManager>(serviceRegistry, eventBus, persistenceAdapter.get());
		modulesRegistered = moduleManager.RegisterModule(*characterManager) && modulesRegistered;
	}
	if (clusterConfig->modules.economy)
	{
		economySystem = std::make_unique<ow::economy::EconomySystem>(serviceRegistry, persistenceAdapter.get());
		modulesRegistered = moduleManager.RegisterModule(*economySystem) && modulesRegistered;
	}
	if (clusterConfig->modules.inventory)
	{
		inventorySystem = std::make_unique<ow::inventory::InventorySystem>(serviceRegistry, persistenceAdapter.get());
		modulesRegistered = moduleManager.RegisterModule(*inventorySystem) && modulesRegistered;
	}
	if (clusterConfig->modules.transfer)
	{
		transferModule = std::make_unique<ow::modules::TransferModule>(serviceRegistry, eventBus);
		modulesRegistered = moduleManager.RegisterModule(*transferModule) && modulesRegistered;
	}
	if (!modulesRegistered || !moduleManager.StartAll())
	{
		// Worker threads capture the modules below, so they must be stopped
		// (their destructors join the threads) before those systems reset.
		playerRouteWorker.reset();
		playerCharacterWorker.reset();
		playerSettingsWorker.reset();
		moduleManager.Clear();
		serviceRegistry = {};
		uiManager.UnregisterAdapter(panoramaAdapter);
		panoramaAdapter.Shutdown();
		transferModule.reset();
		inventorySystem.reset();
		economySystem.reset();
		characterManager.reset();
		coreModule.reset();
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

	// The particle menu needs the entity system plus its transmit filter hook;
	// without gamedata it degrades to being unavailable rather than crashing.
	if (clusterConfig->adapters.particleMenu && ParticleMenuManager::Initialize(g_pSource2GameEntities))
	{
		ParticleMenuManager::SetActionHandler(
			[this](CPlayerSlot slot, const std::string& actionId, const std::string& label)
			{
				HandleMenuAction(slot, actionId, label);
			});
	}
	else
	{
		META_CONPRINTF("[%s] The particle menu is disabled or could not initialize; !menu is unavailable.\n",
					   PLUGIN_LOGTAG);
	}

	META_CONPRINTF("[%s] %s %s loaded.\n", PLUGIN_LOGTAG, PLUGIN_DISPLAY_NAME, PLUGIN_FULL_VERSION);
	return true;
}

bool OpenWorldPlugin::IsGameplayEnabled() const
{
	return loaded && !paused && clusterConfig && clusterConfig->enabled && ow_core_enabled.Get() != 0;
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
	loaded = false;
	paused = false;
	RemoveGameEvents();
	ParticleMenuManager::Shutdown(g_pSource2GameEntities);
	playerRouteWorker.reset();
	playerCharacterWorker.reset();
	playerSettingsWorker.reset();
	moduleManager.Clear();
	hudSystem.ClearAll();
	uiManager.UnregisterAdapter(panoramaAdapter);
	panoramaAdapter.Shutdown();
	eventBus = {};
	serviceRegistry = {};
	transferModule.reset();
	inventorySystem.reset();
	economySystem.reset();
	characterManager.reset();
	coreModule.reset();
	players.clear();
	inputManager.ClearAll();
	currentMapName.clear();
	currentMapOverview.reset();
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
	inputManager.ClearAll();
	ParticleMenuManager::CloseAll("plugin paused");
	for (auto& entry : players)
	{
		entry.second.steamAuthenticated = false;
		entry.second.readyForGameplay = false;
		ResetPlayerRoute(entry.second);
		ResetPlayerSettings(entry.second);
		ResetPlayerCharacter(entry.second);
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
	for (auto& entry : players)
	{
		RefreshSteamAuthorization(entry.second);
	}
	return true;
}

void OpenWorldPlugin::OnLevelInit(char const* mapName, char const* mapEntities, char const* oldLevel, char const* landmarkName, bool loadGame,
							   bool background)
{
	currentMapName = mapName ? mapName : "";
	currentMapOverview.reset();
	if (clusterConfig && clusterConfig->worldMode == ow::core::WorldMode::SingleMap &&
		currentMapName != clusterConfig->worldMap)
	{
		META_CONPRINTF("[%s] Configured single map is %s, but the server started %s.\n", PLUGIN_LOGTAG,
			clusterConfig->worldMap.c_str(), currentMapName.c_str());
	}
	if (engine && (!clusterConfig || clusterConfig->adapters.mapOverview))
	{
		CBufferStringGrowable<512> gameDirectory;
		engine->GetGameDir(gameDirectory);
		const char* gameDirectoryPath = gameDirectory.Get();
		if (gameDirectoryPath && gameDirectoryPath[0] != '\0')
		{
			const std::filesystem::path addonDirectory = std::filesystem::path(gameDirectoryPath) / "addons" / "cs2-openworld";
			mapOverviewCache.SetDirectories(addonDirectory / "maps", addonDirectory / "cache" / "maps");
			const auto overview = mapOverviewCache.Load(currentMapName);
			if (overview)
			{
				currentMapOverview = *overview.overview;
				META_CONPRINTF("[%s] Cached map overview for %s (%ux%u) at %s.\n", PLUGIN_LOGTAG,
					currentMapOverview->mapName.c_str(), currentMapOverview->width, currentMapOverview->height,
					currentMapOverview->imagePath.string().c_str());
				if (!overview.warning.empty())
				{
					META_CONPRINTF("[%s] Map overview warning for %s: %s\n", PLUGIN_LOGTAG,
						currentMapOverview->mapName.c_str(), overview.warning.c_str());
				}
			}
			else if (!currentMapName.empty())
			{
				META_CONPRINTF("[%s] No map overview cached for %s: %s\n", PLUGIN_LOGTAG,
					currentMapName.c_str(), overview.error.c_str());
			}
		}
	}
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
	currentMapOverview.reset();
	ParticleMenuManager::CloseAll("map shutdown");
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

	ProcessPlayerRouteResults();
	ProcessPlayerSettingsResults();
	ProcessPlayerCharacterResults();
	ParticleMenuManager::OnGameFrame(simulating);
	if (clusterConfig && clusterConfig->adapters.hud)
	{
		hudSystem.Refresh();
	}
	for (auto& entry : players)
	{
		PlayerInfo& player = entry.second;
		if (simulating && !player.steamAuthenticated && player.inServer && player.steamId != 0)
		{
			RefreshSteamAuthorization(player);
		}
		if (player.steamAuthenticated)
		{
			EnforcePlayerRoute(player);
			if (simulating)
			{
				CheckAutomaticZoneTransfer(player);
			}
		}
	}
	if (!simulating)
	{
		return;
	}
}

void OpenWorldPlugin::OnClientPutInServer(CPlayerSlot slot, const char* name, int type, uint64 xuid)
{
	if (!slot.IsValid())
	{
		return;
	}
	(void)type;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client entered the server: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	inputManager.RemovePlayer(slot);
	auto& player = players[slot.Get()];
	player.slot = slot.Get();
	player.name = name ? name : "";
	player.steamId = xuid;
	player.inServer = true;
	player.active = false;
	player.fullyConnected = false;
	player.steamAuthenticated = false;
	player.readyForGameplay = false;
	player.zoneTransferArmed = false;
	ResetPlayerRoute(player);
	ResetPlayerSettings(player);
	ResetPlayerCharacter(player);
	hudSystem.UpdateHUDElement(slot, "server", clusterConfig ? clusterConfig->serverId : "unknown");
	hudSystem.UpdateHUDElement(slot, "session", "Connecting");
	hudSystem.UpdateHUDElement(slot, "settings", "Waiting");
}

void OpenWorldPlugin::OnClientActive(CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid)
{
	if (!slot.IsValid())
	{
		return;
	}
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
		player.zoneTransferArmed = false;
		ResetPlayerRoute(player);
		ResetPlayerSettings(player);
		ResetPlayerCharacter(player);
	}
	player.slot = slot.Get();
	player.name = name ? name : "";
	player.steamId = xuid;
	player.active = true;
	RefreshSteamAuthorization(player);
}

void OpenWorldPlugin::OnClientFullyConnect(CPlayerSlot slot)
{
	if (!slot.IsValid())
	{
		return;
	}
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
	if (!slot.IsValid())
	{
		return;
	}
	(void)xuid;
	if (loaded && !paused)
	{
		META_CONPRINTF("[%s] Client disconnected: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(), name ? name : "<unknown>");
	}
	hudSystem.RemovePlayer(slot, false);
	inputManager.RemovePlayer(slot);
	ParticleMenuManager::OnClientDisconnect(slot);
	players.erase(slot.Get());
}

bool OpenWorldPlugin::OnClientCommand(CPlayerSlot slot, const CCommand& command)
{
	if (!IsGameplayEnabled() || !slot.IsValid())
	{
		return false;
	}
	if (clusterConfig->routing.enforceServerAffinity)
	{
		PlayerInfo* player = FindPlayerMutable(slot);
		if (!player || player->routeStatus != PlayerServerRouteStatus::Ready ||
			player->routeServerId != clusterConfig->serverId)
		{
			return false;
		}
	}
	if (command.ArgC() == 1)
	{
		const std::string_view inputCommand(command.Arg(0));
		if (inputCommand == "ow_open_map")
		{
			return clusterConfig && clusterConfig->adapters.input &&
				inputManager.DispatchAction(slot, ow::ui::InputAction::OpenMap);
		}
		if (inputCommand == "ow_open_inventory")
		{
			return clusterConfig && clusterConfig->adapters.input &&
				inputManager.DispatchAction(slot, ow::ui::InputAction::OpenInventory);
		}
		if (inputCommand == "ow_open_menu")
		{
			return clusterConfig && clusterConfig->adapters.particleMenu && ParticleMenuManager::OpenDefaultMenu(slot);
		}
	}

	HandleHudChatCommand(slot, command);
	HandleCharacterChatCommand(slot, command);
	HandleInventoryChatCommand(slot, command);
	HandleMapChatCommand(slot, command);
	HandleBalanceChatCommand(slot, command);
	HandleTransferChatCommand(slot, command);
	HandleMenuChatCommand(slot, command);
	return false;
}

void OpenWorldPlugin::HandleHudChatCommand(CPlayerSlot slot, const CCommand& command)
{
	if (!slot.IsValid() || command.ArgC() < 2)
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
	message >> verb;
	if (verb != "!hud")
	{
		return;
	}
	if (!clusterConfig || !clusterConfig->adapters.hud)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The HUD adapter is disabled by server configuration.");
		return;
	}

	std::string operation;
	std::string extra;
	if (!(message >> operation))
	{
		operation = "toggle";
	}
	if ((message >> extra) || (operation != "on" && operation != "off" && operation != "toggle"))
	{
		if (uiManager.ShowText(slot, ow::ui::UITextChannel::Chat, "Usage: !hud [on|off|toggle]") !=
			ow::ui::UIStatus::Success && engine)
		{
			engine->ClientPrintf(slot, "Usage: !hud [on|off|toggle]\n");
		}
		return;
	}

	const bool enabled = operation == "on" ||
		(operation == "toggle" && !hudSystem.IsHUDEnabled(slot));
	if (!hudSystem.SetHUDEnabled(slot, enabled))
	{
		return;
	}
	const char* statusText = enabled ? "Open World text HUD enabled" : "Open World text HUD disabled";
	if (uiManager.ShowText(slot, ow::ui::UITextChannel::Notification, statusText) != ow::ui::UIStatus::Success && engine)
	{
		const std::string fallbackMessage = std::string(statusText) + '\n';
		engine->ClientPrintf(slot, fallbackMessage.c_str());
	}
}

void OpenWorldPlugin::HandleMenuChatCommand(CPlayerSlot slot, const CCommand& command)
{
	std::string chatText;
	if (!slot.IsValid() || !BuildChatText(command, chatText))
	{
		return;
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string action;
	message >> verb;
	if (verb != "!menu" && verb != "!pmenu")
	{
		return;
	}
	if (!clusterConfig || !clusterConfig->adapters.particleMenu)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The particle menu is disabled by server configuration.");
		return;
	}
	if (message >> action)
	{
		if (action != "close")
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !menu [close]");
			return;
		}
		if (!ParticleMenuManager::CloseMenu(slot, true, "command"))
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "No particle menu is open.");
		}
		return;
	}

	if (ParticleMenuManager::IsMenuOpen(slot))
	{
		ParticleMenuManager::CloseMenu(slot, true, "command_toggle");
		return;
	}
	if (!ParticleMenuManager::OpenDefaultMenu(slot))
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The particle menu is not available right now.");
	}
}

void OpenWorldPlugin::HandleMenuAction(CPlayerSlot slot, std::string_view actionId, std::string_view label)
{
	if (!IsGameplayEnabled() || actionId.empty())
	{
		return;
	}

	if (actionId == "inventory")
	{
		HandleOpenInventory(slot);
		return;
	}
	if (actionId == "profile" || actionId == "settings")
	{
		const PlayerInfo* player = FindPlayer(slot);
		if (player)
		{
			hudSystem.UpdateHUDElement(slot, "character",
				player->character && player->readyForGameplay ? "Character loaded" : "Character pending");
		}
	}

	SendPlayerText(slot, ow::ui::UITextChannel::Chat, std::string("Menu: ") + std::string(label) + " (" + std::string(actionId) + ").");
}

void OpenWorldPlugin::SendPlayerText(CPlayerSlot slot, ow::ui::UITextChannel channel, std::string_view text) const
{
	if (text.empty() || uiManager.ShowText(slot, channel, text) == ow::ui::UIStatus::Success || !engine)
	{
		return;
	}
	const std::string fallbackMessage = std::string(text) + '\n';
	engine->ClientPrintf(slot, fallbackMessage.c_str());
}

void OpenWorldPlugin::HandleCharacterChatCommand(CPlayerSlot slot, const CCommand& command)
{
	std::string chatText;
	if (!slot.IsValid() || !BuildChatText(command, chatText))
	{
		return;
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string action;
	message >> verb;
	if (verb != "!character")
	{
		return;
	}
	if (!clusterConfig || !clusterConfig->modules.characters || !clusterConfig->adapters.postgresql)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The character module is disabled by server configuration.");
		return;
	}
	if (!(message >> action))
	{
		action = "list";
	}

	PlayerInfo* player = FindPlayerMutable(slot);
	const CSteamID* authenticatedSteamId = utils::GetClientSteamID(engine, slot);
	if (!player || !player->inServer || !player->fullyConnected || !player->steamAuthenticated || !engine ||
		!authenticatedSteamId || !authenticatedSteamId->IsValid() ||
		authenticatedSteamId->ConvertToUint64() != player->steamId || !utils::IsClientFullyAuthenticated(engine, slot))
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Character commands require a fully authenticated session.");
		return;
	}

	if (action == "list")
	{
		std::string extra;
		if (message >> extra)
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !character list|create <name>|select <id>");
			return;
		}
		if (player->characters.empty())
		{
			if (player->characterStatus == PlayerCharacterStatus::NeedsCreation)
			{
				SendPlayerText(slot, ow::ui::UITextChannel::Chat, "No character exists. Create one with !character create <name>.");
			}
			else if (player->characterStatus == PlayerCharacterStatus::Failed)
			{
				SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Character data is unavailable; check the server logs.");
			}
			else
			{
				SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Character list is still loading.");
			}
			return;
		}

		SendPlayerText(slot, ow::ui::UITextChannel::Chat,
					   "Characters (select with !character select <id>):");
		for (const auto& character : player->characters)
		{
			std::string safeName = character.displayName;
			for (char& characterByte : safeName)
			{
				const auto byte = static_cast<unsigned char>(characterByte);
				if (byte < 0x20 || byte == 0x7f)
				{
					characterByte = ' ';
				}
			}
			std::string line = safeName + " [" + character.characterId + "]";
			if (character.characterId == player->characterId)
			{
				line += " (active)";
			}
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, line);
		}
		return;
	}

	if (action == "create")
	{
		std::string displayName;
		std::getline(message, displayName);
		displayName = TrimChatWhitespace(std::move(displayName));
		if (!IsSafeCharacterName(displayName))
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !character create <name (1-64 bytes)>");
			return;
		}
		if (player->characterStatus == PlayerCharacterStatus::Loading)
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "A character request is already in progress.");
			return;
		}
		RequestCharacterCreate(*player, displayName);
		if (player->characterStatus == PlayerCharacterStatus::Loading)
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Creating your character...");
		}
		return;
	}

	if (action == "select")
	{
		std::string characterId;
		std::string extra;
		if (!(message >> characterId) || (message >> extra))
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !character select <id>");
			return;
		}
		const auto character = std::find_if(player->characters.begin(), player->characters.end(),
												[&characterId](const auto& candidate) { return candidate.characterId == characterId; });
		if (character == player->characters.end())
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "That character is not in your character list.");
			return;
		}
		if (player->characterStatus == PlayerCharacterStatus::Loading)
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "A character request is already in progress.");
			return;
		}
		RequestCharacterLoad(*player, characterId);
		if (player->characterStatus == PlayerCharacterStatus::Loading)
		{
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Loading character...");
		}
		return;
	}

	SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !character list|create <name>|select <id>");
}

void OpenWorldPlugin::HandleInventoryChatCommand(CPlayerSlot slot, const CCommand& command)
{
	std::string chatText;
	if (!slot.IsValid() || !BuildChatText(command, chatText))
	{
		return;
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string extra;
	message >> verb;
	if (verb != "!inventory")
	{
		return;
	}
	if (message >> extra)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !inventory");
		return;
	}
	if (!clusterConfig || !clusterConfig->adapters.input)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The input adapter is disabled by server configuration.");
		return;
	}
	inputManager.DispatchAction(slot, ow::ui::InputAction::OpenInventory);
}

void OpenWorldPlugin::HandleMapChatCommand(CPlayerSlot slot, const CCommand& command)
{
	std::string chatText;
	if (!slot.IsValid() || !BuildChatText(command, chatText))
	{
		return;
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string extra;
	message >> verb;
	if (verb != "!map")
	{
		return;
	}
	if (message >> extra)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !map");
		return;
	}
	if (!clusterConfig || !clusterConfig->adapters.input)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The input adapter is disabled by server configuration.");
		return;
	}
	inputManager.DispatchAction(slot, ow::ui::InputAction::OpenMap);
}

void OpenWorldPlugin::HandleInputAction(CPlayerSlot slot, ow::ui::InputAction action)
{
	if (!IsGameplayEnabled() || !clusterConfig->adapters.input || !slot.IsValid())
	{
		return;
	}
	if (clusterConfig->routing.enforceServerAffinity)
	{
		const PlayerInfo* player = FindPlayer(slot);
		if (!player || player->routeStatus != PlayerServerRouteStatus::Ready ||
			player->routeServerId != clusterConfig->serverId)
		{
			return;
		}
	}
	switch (action)
	{
	case ow::ui::InputAction::OpenMap:
		HandleOpenMap(slot);
		break;
	case ow::ui::InputAction::OpenInventory:
		HandleOpenInventory(slot);
		break;
	}
}

void OpenWorldPlugin::HandleOpenInventory(CPlayerSlot slot)
{
	if (!clusterConfig || !clusterConfig->modules.inventory || !clusterConfig->adapters.postgresql)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The inventory module is disabled by server configuration.");
		return;
	}
	const PlayerInfo* player = FindPlayer(slot);
	const CSteamID* authenticatedSteamId = utils::GetClientSteamID(engine, slot);
	if (!player || !player->inServer || !player->fullyConnected || !player->steamAuthenticated || !engine ||
		!authenticatedSteamId || !authenticatedSteamId->IsValid() ||
		authenticatedSteamId->ConvertToUint64() != player->steamId || !utils::IsClientFullyAuthenticated(engine, slot) ||
		player->characterStatus != PlayerCharacterStatus::Ready || !player->character)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Select or create a character first with !character.");
		return;
	}
	if (player->character->inventoryJson.empty() || player->character->inventoryJson == "[]")
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Your inventory is empty.");
		return;
	}

	std::string inventoryText = "Inventory revision " + std::to_string(player->character->inventoryRevision) + ":\n" +
							   player->character->inventoryJson;
	if (inventoryText.size() > ow::adapters::PanoramaAdapter::MaxTextBytes)
	{
		inventoryText.resize(ow::adapters::PanoramaAdapter::MaxTextBytes - 3);
		inventoryText += "...";
	}
	SendPlayerText(slot, ow::ui::UITextChannel::Console, inventoryText);
	SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Inventory snapshot printed to your console.");
}

void OpenWorldPlugin::HandleOpenMap(CPlayerSlot slot)
{
	std::string mapName = currentMapName.substr(0, 128);
	for (char& mapByte : mapName)
	{
		const auto byte = static_cast<unsigned char>(mapByte);
		if (byte < 0x20 || byte == 0x7f)
		{
			mapByte = ' ';
		}
	}
	if (!mapName.empty())
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Center, "Current map: " + mapName);
	}
	const std::string mapStatus = !clusterConfig || !clusterConfig->adapters.mapOverview
		? "Map overview caching is disabled by server configuration."
		: currentMapOverview
			? "Map overview cached on the server; a compiled map-specific particle asset is required to display it."
			: "No valid map overview PNG is cached; add one under addons/cs2-openworld/maps before packaging.";
	SendPlayerText(slot, ow::ui::UITextChannel::Console, mapStatus);
}

void OpenWorldPlugin::HandleBalanceChatCommand(CPlayerSlot slot, const CCommand& command)
{
	std::string chatText;
	if (!slot.IsValid() || !BuildChatText(command, chatText))
	{
		return;
	}
	std::istringstream message(chatText);
	std::string verb;
	std::string extra;
	message >> verb;
	if (verb != "!balance")
	{
		return;
	}
	if (message >> extra)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Usage: !balance");
		return;
	}
	if (!clusterConfig || !clusterConfig->modules.economy || !clusterConfig->adapters.postgresql)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "The economy module is disabled by server configuration.");
		return;
	}
	const PlayerInfo* player = FindPlayer(slot);
	const CSteamID* authenticatedSteamId = utils::GetClientSteamID(engine, slot);
	if (!player || !player->inServer || !player->fullyConnected || !player->steamAuthenticated || !engine ||
		!authenticatedSteamId || !authenticatedSteamId->IsValid() ||
		authenticatedSteamId->ConvertToUint64() != player->steamId || !utils::IsClientFullyAuthenticated(engine, slot) ||
		player->characterStatus != PlayerCharacterStatus::Ready || !player->character)
	{
		SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Select or create a character first with !character.");
		return;
	}
	SendPlayerText(slot, ow::ui::UITextChannel::Chat,
				   "Balance: " + std::to_string(player->character->balance));
}

void OpenWorldPlugin::HandleTransferChatCommand(CPlayerSlot slot, const CCommand& command)
{
	if (!slot.IsValid() || !engine || !clusterConfig || command.ArgC() < 2)
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
	if (!clusterConfig->modules.transfer || !transferModule)
	{
		engine->ClientPrintf(slot, "The transfer module is disabled by server configuration.\n");
		return;
	}
	if (!(message >> destinationId) || (message >> extra))
	{
		engine->ClientPrintf(slot, "Usage: !go <server_id>\n");
		return;
	}

	PlayerInfo* player = FindPlayerMutable(slot);
	const CSteamID* steamId = utils::GetClientSteamID(engine, slot);
	if (!player || !player->inServer || !player->fullyConnected || !player->steamAuthenticated ||
		!steamId || !steamId->IsValid() || steamId->ConvertToUint64() != player->steamId ||
		!utils::IsClientFullyAuthenticated(engine, slot))
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

	if (clusterConfig->routing.enforceServerAffinity)
	{
		if (!RequestPlayerRouteAssignment(*player, destination->id))
		{
			engine->ClientPrintf(slot, "Could not safely update your server assignment. Please try again.\n");
			return;
		}
		engine->ClientPrintf(slot, "Your server assignment is being saved. You will be redirected shortly.\n");
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
	if (!slot.IsValid())
	{
		return;
	}
}

const OpenWorldPlugin::PlayerInfo* OpenWorldPlugin::FindPlayer(CPlayerSlot slot) const
{
	if (!slot.IsValid())
	{
		return nullptr;
	}
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
		ResetPlayerRoute(player);
		ResetPlayerSettings(player);
		ResetPlayerCharacter(player);
		hudSystem.UpdateHUDElement(slot, "session", "Waiting for authentication");
		return;
	}

	if (!utils::IsClientFullyAuthenticated(engine, slot))
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerRoute(player);
		ResetPlayerSettings(player);
		ResetPlayerCharacter(player);
		hudSystem.UpdateHUDElement(slot, "session", "Authenticating");
		return;
	}

	if (player.steamId == 0)
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerRoute(player);
		ResetPlayerSettings(player);
		ResetPlayerCharacter(player);
		hudSystem.UpdateHUDElement(slot, "session", "Authenticating");
		return;
	}

	const CSteamID* authenticatedSteamId = utils::GetClientSteamID(engine, slot);
	if (!authenticatedSteamId || !authenticatedSteamId->IsValid() ||
		authenticatedSteamId->ConvertToUint64() != player.steamId)
	{
		player.steamAuthenticated = false;
		player.readyForGameplay = false;
		ResetPlayerRoute(player);
		ResetPlayerSettings(player);
		ResetPlayerCharacter(player);
		hudSystem.UpdateHUDElement(slot, "session", "Authenticating");
		return;
	}

	if (!player.steamAuthenticated)
	{
		META_CONPRINTF("[%s] Steam authorization confirmed for player slot %d.\n", PLUGIN_LOGTAG, player.slot);
	}
	player.steamAuthenticated = true;
	if (clusterConfig && clusterConfig->routing.enforceServerAffinity)
	{
		if (player.active && player.routeStatus == PlayerServerRouteStatus::NotRequested)
		{
			RequestPlayerRoute(player);
		}
		player.readyForGameplay = player.active && player.fullyConnected &&
			player.routeStatus == PlayerServerRouteStatus::Ready &&
			player.routeServerId == clusterConfig->serverId;
	}
	else
	{
		player.readyForGameplay = player.active && player.fullyConnected;
	}
	const char* sessionStatus = player.readyForGameplay ? "Ready" :
		player.routeStatus == PlayerServerRouteStatus::Loading ? "Checking server" :
		player.routeStatus == PlayerServerRouteStatus::Redirecting ? "Redirecting" :
		player.routeStatus == PlayerServerRouteStatus::Failed ? "Route unavailable" : "Authorized";
	hudSystem.UpdateHUDElement(slot, "session", sessionStatus);
	if (player.active && player.fullyConnected)
	{
		if (clusterConfig && clusterConfig->routing.enforceServerAffinity &&
			(player.routeStatus != PlayerServerRouteStatus::Ready || player.routeServerId != clusterConfig->serverId))
		{
			return;
		}
		if (clusterConfig && clusterConfig->adapters.postgresql)
		{
			if (player.settingsLoadStatus == PlayerSettingsLoadStatus::NotRequested)
			{
				RequestPlayerSettings(player);
			}
			if (clusterConfig->modules.characters && player.characterStatus == PlayerCharacterStatus::NotRequested)
			{
				RequestPlayerCharacters(player);
			}
		}
		else
		{
			player.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
			player.settingsLoadError = "PostgreSQL adapter is disabled by configuration.";
			hudSystem.UpdateHUDElement(slot, "settings", "Disabled");
		}
		if (!clusterConfig || !clusterConfig->modules.characters)
		{
			player.characterStatus = PlayerCharacterStatus::Failed;
			player.characterLoadError = "Character module is disabled by configuration.";
			hudSystem.UpdateHUDElement(slot, "character", "Disabled");
		}
	}
}

void OpenWorldPlugin::ResetPlayerRoute(PlayerInfo& player)
{
	player.routeRequestId = 0;
	player.routeStatus = PlayerServerRouteStatus::NotRequested;
	player.routeOperation = ow::persistence::PlayerRouteOperation::Resolve;
	player.routeServerId.clear();
}

std::uint64_t OpenWorldPlugin::NextPlayerRouteRequestId()
{
	++nextPlayerRouteRequestId;
	if (nextPlayerRouteRequestId == 0)
	{
		++nextPlayerRouteRequestId;
	}
	return nextPlayerRouteRequestId;
}

void OpenWorldPlugin::RequestPlayerRoute(PlayerInfo& player)
{
	if (!clusterConfig || !clusterConfig->routing.enforceServerAffinity || !playerRouteWorker ||
		player.steamId == 0)
	{
		player.routeStatus = PlayerServerRouteStatus::Failed;
		player.readyForGameplay = false;
		player.routeServerId.clear();
		META_CONPRINTF("[%s] Player route service is unavailable for slot %d.\n", PLUGIN_LOGTAG, player.slot);
		return;
	}

	player.routeRequestId = NextPlayerRouteRequestId();
	player.routeOperation = ow::persistence::PlayerRouteOperation::Resolve;
	player.routeStatus = PlayerServerRouteStatus::Loading;
	player.routeServerId.clear();
	player.readyForGameplay = false;
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "session", "Checking server");
	if (!playerRouteWorker->RequestResolve(player.routeRequestId, player.steamId, clusterConfig->environment,
										   clusterConfig->serverId, clusterConfig->routing.lobbyServerId))
	{
		player.routeStatus = PlayerServerRouteStatus::Failed;
		META_CONPRINTF("[%s] Could not queue server route lookup for slot %d.\n", PLUGIN_LOGTAG, player.slot);
	}
}

bool OpenWorldPlugin::RequestPlayerRouteAssignment(PlayerInfo& player, std::string_view destinationServerId)
{
	if (!clusterConfig || !clusterConfig->routing.enforceServerAffinity || !playerRouteWorker ||
		player.routeStatus != PlayerServerRouteStatus::Ready || player.routeServerId != clusterConfig->serverId)
	{
		return false;
	}

	player.routeRequestId = NextPlayerRouteRequestId();
	player.routeOperation = ow::persistence::PlayerRouteOperation::Assign;
	player.routeStatus = PlayerServerRouteStatus::Loading;
	player.readyForGameplay = false;
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "session", "Changing server");
	if (!playerRouteWorker->RequestAssign(player.routeRequestId, player.steamId, clusterConfig->environment,
										  destinationServerId))
	{
		player.routeStatus = PlayerServerRouteStatus::Ready;
		player.readyForGameplay = player.active && player.fullyConnected;
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "session",
								   player.readyForGameplay ? "Ready" : "Authorized");
		return false;
	}
	return true;
}

void OpenWorldPlugin::ProcessPlayerRouteResults()
{
	if (!playerRouteWorker)
	{
		return;
	}
	while (auto result = playerRouteWorker->TryTakeResult())
	{
		auto found = std::find_if(players.begin(), players.end(), [&result](const auto& entry) {
			return entry.second.routeRequestId == result->requestId &&
				   entry.second.steamId == result->steamId &&
				   entry.second.routeOperation == result->operation;
		});
		if (found == players.end() || !found->second.inServer || !found->second.steamAuthenticated ||
			found->second.routeStatus != PlayerServerRouteStatus::Loading)
		{
			continue;
		}

		PlayerInfo& player = found->second;
		if (!result->error.empty())
		{
			player.routeStatus = PlayerServerRouteStatus::Failed;
			player.readyForGameplay = false;
			hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "session", "Route unavailable");
			META_CONPRINTF("[%s] Server route %s failed for slot %d: %s\n", PLUGIN_LOGTAG,
						   result->operation == ow::persistence::PlayerRouteOperation::Resolve ? "lookup" : "update",
						   player.slot, result->error.c_str());
			if (engine)
			{
				engine->ClientPrintf(CPlayerSlot(player.slot),
					"Could not verify your server assignment. Please reconnect later or contact an administrator.\n");
			}
			continue;
		}

		if (!clusterConfig || !clusterConfig->FindServer(result->serverId))
		{
			player.routeStatus = PlayerServerRouteStatus::Failed;
			player.readyForGameplay = false;
			META_CONPRINTF("[%s] Database route references unconfigured server '%s' for slot %d.\n",
						   PLUGIN_LOGTAG, result->serverId.c_str(), player.slot);
			if (engine)
			{
				engine->ClientPrintf(CPlayerSlot(player.slot),
					"Your saved server assignment is unavailable. Please contact an administrator.\n");
			}
			continue;
		}

		player.routeServerId = std::move(result->serverId);
		player.routeStatus = PlayerServerRouteStatus::Ready;
		EnforcePlayerRoute(player);
		if (player.routeStatus == PlayerServerRouteStatus::Ready &&
			player.routeServerId == clusterConfig->serverId)
		{
			RefreshSteamAuthorization(player);
		}
	}
}

void OpenWorldPlugin::EnforcePlayerRoute(PlayerInfo& player)
{
	if (!clusterConfig || !clusterConfig->routing.enforceServerAffinity ||
		player.routeStatus != PlayerServerRouteStatus::Ready)
	{
		return;
	}
	if (player.routeServerId == clusterConfig->serverId)
	{
		player.readyForGameplay = player.active && player.fullyConnected;
		return;
	}

	player.readyForGameplay = false;
	if (!player.fullyConnected)
	{
		return;
	}
	const auto* destination = clusterConfig->FindServer(player.routeServerId);
	if (!destination || !utils::IsClientFullyAuthenticated(engine, CPlayerSlot(player.slot)))
	{
		player.routeStatus = PlayerServerRouteStatus::Failed;
		META_CONPRINTF("[%s] Cannot redirect slot %d to its assigned server '%s'.\n", PLUGIN_LOGTAG,
					   player.slot, player.routeServerId.c_str());
		return;
	}

	ow::adapters::ClientRedirector redirector(engine);
	if (!redirector.Redirect(CPlayerSlot(player.slot), destination->connect))
	{
		player.routeStatus = PlayerServerRouteStatus::Failed;
		META_CONPRINTF("[%s] Redirect failed for slot %d to assigned server '%s'.\n", PLUGIN_LOGTAG,
					   player.slot, destination->id.c_str());
		return;
	}
	player.routeStatus = PlayerServerRouteStatus::Redirecting;
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "session", "Redirecting");
	META_CONPRINTF("[%s] Redirected slot %d from %s to assigned server %s.\n", PLUGIN_LOGTAG,
				   player.slot, clusterConfig->serverId.c_str(), destination->id.c_str());
}

void OpenWorldPlugin::CheckAutomaticZoneTransfer(PlayerInfo& player)
{
	if (!clusterConfig || !clusterConfig->routing.enforceServerAffinity || !clusterConfig->modules.transfer ||
		!player.inServer || !player.active || !player.fullyConnected || !player.steamAuthenticated ||
		player.routeStatus != PlayerServerRouteStatus::Ready || player.routeServerId != clusterConfig->serverId ||
		!utils::IsClientFullyAuthenticated(engine, CPlayerSlot(player.slot)) || !g_pPlayerManager)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now < player.nextZoneTransferCheck)
	{
		return;
	}
	player.nextZoneTransferCheck = now + std::chrono::milliseconds(250);

	Player* sdkPlayer = g_pPlayerManager->ToPlayer(CPlayerSlot(player.slot));
	CCSPlayerPawn* pawn = sdkPlayer ? sdkPlayer->GetPlayerPawn() : nullptr;
	if (!pawn || !pawn->IsAlive())
	{
		return;
	}
	CBodyComponent* body = pawn->m_CBodyComponent();
	CGameSceneNode* sceneNode = body ? body->m_pSceneNode() : nullptr;
	if (!sceneNode)
	{
		return;
	}
	const Vector origin = sceneNode->m_vecAbsOrigin();
	if (!std::isfinite(origin.x) || !std::isfinite(origin.y))
	{
		return;
	}

	const ow::core::ZoneOwnership* currentZone =
		clusterConfig->FindZone(currentMapName, origin.x, origin.y);
	if (currentZone && currentZone->serverId == clusterConfig->serverId)
	{
		player.zoneTransferArmed = true;
		return;
	}
	if (!player.zoneTransferArmed)
	{
		return;
	}

	const ow::core::ZoneOwnership* destination =
		clusterConfig->FindTransitionZone(currentMapName, clusterConfig->serverId, origin.x, origin.y);
	if (!destination || destination->serverId == clusterConfig->serverId)
	{
		return;
	}
	if (RequestPlayerRouteAssignment(player, destination->serverId))
	{
		engine->ClientPrintf(CPlayerSlot(player.slot), "You reached the edge of this area. Connecting to the next server...\n");
		META_CONPRINTF("[%s] Player slot %d crossed from zone on %s to server %s.\n", PLUGIN_LOGTAG,
					   player.slot, clusterConfig->serverId.c_str(), destination->serverId.c_str());
	}
}

void OpenWorldPlugin::ResetPlayerSettings(PlayerInfo& player)
{
	player.settingsRequestId = 0;
	player.settingsLoadStatus = PlayerSettingsLoadStatus::NotRequested;
	player.settings.reset();
	player.settingsLoadError.clear();
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "settings", "Waiting");
}

void OpenWorldPlugin::ResetPlayerCharacter(PlayerInfo& player)
{
	player.characterRequestId = 0;
	player.characterStatus = PlayerCharacterStatus::NotRequested;
	player.characters.clear();
	player.character.reset();
	player.characterLoadError.clear();
	player.characterId.clear();
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Waiting");
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "wallet", "Unavailable");
}

std::uint64_t OpenWorldPlugin::NextCharacterRequestId()
{
	++nextCharacterRequestId;
	if (nextCharacterRequestId == 0)
	{
		++nextCharacterRequestId;
	}
	return nextCharacterRequestId;
}

void OpenWorldPlugin::RequestPlayerCharacters(PlayerInfo& player)
{
	if (!playerCharacterWorker || player.steamId == 0)
	{
		player.characterStatus = PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character loading service or authenticated Steam ID is unavailable.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Unavailable");
		return;
	}
	player.characterRequestId = NextCharacterRequestId();
	player.characterStatus = PlayerCharacterStatus::Loading;
	player.characterLoadError.clear();
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Loading");
	if (!playerCharacterWorker->RequestList(player.characterRequestId, player.steamId))
	{
		player.characterStatus = PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character load queue is full or shutting down.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Unavailable");
		META_CONPRINTF("[%s] Could not queue character list for slot %d: %s\n", PLUGIN_LOGTAG, player.slot,
					   player.characterLoadError.c_str());
	}
}

void OpenWorldPlugin::RequestCharacterCreate(PlayerInfo& player, std::string_view displayName)
{
	if (!playerCharacterWorker || player.steamId == 0)
	{
		player.characterStatus = PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character creation service is unavailable.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Unavailable");
		return;
	}
	player.characterRequestId = NextCharacterRequestId();
	player.characterStatus = PlayerCharacterStatus::Loading;
	player.characterLoadError.clear();
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Creating");
	if (!playerCharacterWorker->RequestCreate(player.characterRequestId, player.steamId, displayName))
	{
		player.characterStatus = player.character ? PlayerCharacterStatus::Ready : PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character creation queue is full or the name is invalid.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character",
								   player.character ? player.character->displayName : "Unavailable");
		META_CONPRINTF("[%s] Could not queue character creation for slot %d: %s\n", PLUGIN_LOGTAG, player.slot,
					   player.characterLoadError.c_str());
	}
}

void OpenWorldPlugin::RequestCharacterLoad(PlayerInfo& player, std::string_view characterId)
{
	if (!playerCharacterWorker || player.steamId == 0)
	{
		player.characterStatus = PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character loading service is unavailable.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Unavailable");
		return;
	}
	player.characterRequestId = NextCharacterRequestId();
	player.characterStatus = PlayerCharacterStatus::Loading;
	player.characterLoadError.clear();
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character", "Loading");
	if (!playerCharacterWorker->RequestLoad(player.characterRequestId, player.steamId, characterId))
	{
		player.characterStatus = player.character ? PlayerCharacterStatus::Ready : PlayerCharacterStatus::Failed;
		player.characterLoadError = "Character load queue is full or the character ID is invalid.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "character",
							   player.character ? player.character->displayName : "Unavailable");
		META_CONPRINTF("[%s] Could not queue character load for slot %d: %s\n", PLUGIN_LOGTAG, player.slot,
					   player.characterLoadError.c_str());
	}
}

void OpenWorldPlugin::RequestPlayerSettings(PlayerInfo& player)
{
	if (!playerSettingsWorker || player.steamId == 0)
	{
		player.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
		player.settingsLoadError = "Settings loading service or authenticated Steam ID is unavailable.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "settings", "Unavailable");
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
	hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "settings", "Loading");
	if (!playerSettingsWorker->Request(player.settingsRequestId, player.steamId))
	{
		player.settingsLoadStatus = PlayerSettingsLoadStatus::Failed;
		player.settingsLoadError = "Settings load queue is full or shutting down.";
		hudSystem.UpdateHUDElement(CPlayerSlot(player.slot), "settings", "Unavailable");
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
			hudSystem.UpdateHUDElement(CPlayerSlot(found->second.slot), "settings", "Failed");
			META_CONPRINTF("[%s] Player settings load failed for slot %d: %s\n", PLUGIN_LOGTAG,
						   found->second.slot, result->error.c_str());
			continue;
		}
		found->second.settings = std::move(result->settings);
		found->second.settingsLoadStatus = PlayerSettingsLoadStatus::Ready;
		found->second.settingsLoadError.clear();
		hudSystem.UpdateHUDElement(CPlayerSlot(found->second.slot), "settings", "Ready");
		META_CONPRINTF("[%s] Player settings loaded for slot %d (revision %llu).\n", PLUGIN_LOGTAG,
					   found->second.slot,
					   static_cast<unsigned long long>(found->second.settings->revision));
	}
}

void OpenWorldPlugin::ProcessPlayerCharacterResults()
{
	if (!playerCharacterWorker)
	{
		return;
	}
	while (auto result = playerCharacterWorker->TryTakeResult())
	{
		auto found = std::find_if(players.begin(), players.end(), [&result](const auto& entry) {
			return entry.second.characterRequestId == result->requestId && entry.second.steamId == result->steamId;
		});
		if (found == players.end() || !found->second.inServer || !found->second.steamAuthenticated ||
			found->second.characterStatus != PlayerCharacterStatus::Loading)
		{
			continue;
		}

		PlayerInfo& player = found->second;
		const CPlayerSlot slot(player.slot);
		if (!result->error.empty())
		{
			player.characterLoadError = result->error;
			player.characterStatus = player.character ? PlayerCharacterStatus::Ready : PlayerCharacterStatus::Failed;
			hudSystem.UpdateHUDElement(slot, "character",
								   player.character ? player.character->displayName : "Unavailable");
			META_CONPRINTF("[%s] Character request failed for slot %d: %s\n", PLUGIN_LOGTAG, player.slot,
						   result->error.c_str());
			SendPlayerText(slot, ow::ui::UITextChannel::Chat, "Character request failed; check the server logs.");
			continue;
		}

		if (result->operation == ow::persistence::PlayerCharacterOperation::List)
		{
			player.characters = std::move(result->characters);
			if (player.characters.empty())
			{
				player.characterStatus = PlayerCharacterStatus::NeedsCreation;
				hudSystem.UpdateHUDElement(slot, "character", "Create a character");
				SendPlayerText(slot, ow::ui::UITextChannel::Chat,
							   "No character found. Create one with !character create <name>.");
			}
			else if (player.characters.size() == 1)
			{
				RequestCharacterLoad(player, player.characters.front().characterId);
			}
			else
			{
				player.characterStatus = PlayerCharacterStatus::SelectionRequired;
				hudSystem.UpdateHUDElement(slot, "character", "Select a character");
				SendPlayerText(slot, ow::ui::UITextChannel::Chat,
							   "Multiple characters found. Use !character list and !character select <id>.");
			}
			continue;
		}

		if (!result->character)
		{
			player.characterStatus = PlayerCharacterStatus::Failed;
			player.characterLoadError = "Character request returned no data.";
			hudSystem.UpdateHUDElement(slot, "character", "Unavailable");
			continue;
		}
		player.character = std::move(result->character);
		player.characterId = player.character->characterId;
		player.characterStatus = PlayerCharacterStatus::Ready;
		player.characterLoadError.clear();
		if (result->operation == ow::persistence::PlayerCharacterOperation::Create)
		{
			const auto existing = std::find_if(player.characters.begin(), player.characters.end(), [&player](const auto& entry) {
				return entry.characterId == player.characterId;
			});
			if (existing == player.characters.end())
			{
				player.characters.push_back({player.character->characterId, player.character->displayName});
			}
		}
		std::string displayName = player.character->displayName;
		for (char& characterByte : displayName)
		{
			const auto byte = static_cast<unsigned char>(characterByte);
			if (byte < 0x20 || byte == 0x7f)
			{
				characterByte = ' ';
			}
		}
		hudSystem.UpdateHUDElement(slot, "character", displayName);
		hudSystem.UpdateHUDElement(slot, "wallet", std::to_string(player.character->balance));
		SendPlayerText(slot, ow::ui::UITextChannel::Chat,
					   "Active character: " + displayName + " (balance " + std::to_string(player.character->balance) + ").");
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
	gameEventManager = interfaces::pGameEventManager;
	if (!gameEventManager)
	{
		META_CONPRINTF("[%s] The game event manager is unavailable; player_spawn tracking is disabled.\n", PLUGIN_LOGTAG);
		return;
	}
	if (gameEventManager->AddListener(this, "player_spawn", true))
	{
		gameEventsRegistered = true;
	}
	else
	{
		META_CONPRINTF("[%s] player_spawn events could not be registered.\n", PLUGIN_LOGTAG);
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
		// In CS2 the event's userid field is the player slot.
		HandlePlayerSpawned(CPlayerSlot(event->GetInt("userid")));
	}
}

void OpenWorldPlugin::HandlePlayerSpawned(CPlayerSlot slot)
{
	if (!IsGameplayEnabled() || !slot.IsValid())
	{
		return;
	}
	PlayerInfo* player = FindPlayerMutable(slot);
	if (!player)
	{
		return;
	}

	const char* sessionText = player->characterStatus == PlayerCharacterStatus::Ready && player->character
		? "Character loaded"
		: "Character pending";
	hudSystem.UpdateHUDElement(slot, "session", sessionText);
	if (player->character)
	{
		hudSystem.UpdateHUDElement(slot, "wallet", std::to_string(player->character->balance));
	}

	META_CONPRINTF("[%s] Player spawned: slot %d, name %s.\n", PLUGIN_LOGTAG, slot.Get(),
		player->name.empty() ? "<unknown>" : player->name.c_str());
}

OpenWorldPlugin::PlayerInfo* OpenWorldPlugin::FindPlayerMutable(CPlayerSlot slot)
{
	if (!slot.IsValid())
	{
		return nullptr;
	}
	auto player = players.find(slot.Get());
	return player == players.end() ? nullptr : &player->second;
}
