#pragma once

#include <ISmmPlugin.h>
#include <eiface.h>
#include <igameevents.h>
#include <tier1/convar.h>
#include <playerslot.h>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "adapters/persistence/postgres_adapter.h"
#include "cluster_config.h"
#include "event_bus.h"
#include "module_manager.h"
#include "service_registry.h"
#include "version_gen.h"
#include "modules/persistence/player_settings_worker.h"
#include "modules/core/core_module.h"
#include "modules/character/character_manager.h"
#include "modules/transfer/transfer_module.h"

PLUGIN_GLOBALVARS();

enum class PlayerSettingsLoadStatus
{
	NotRequested,
	Loading,
	Ready,
	Failed
};

class OpenWorldPlugin final : public ISmmPlugin, public IMetamodListener, public IGameEventListener2
{
public:
	struct PlayerInfo
	{
		int slot {};
		std::string name;
		uint64 steamId {};
		bool inServer {};
		bool active {};
		bool fullyConnected {};
		bool steamAuthenticated {};
		bool readyForGameplay {};
		std::uint64_t settingsRequestId {};
		PlayerSettingsLoadStatus settingsLoadStatus {PlayerSettingsLoadStatus::NotRequested};
		std::optional<ow::adapters::PlayerSettings> settings;
		std::string settingsLoadError;
		std::string characterId;
	};

	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
	void AllPluginsLoaded() override;
	bool QueryRunning(char* error, size_t maxlen) override;
	bool Unload(char* error, size_t maxlen) override;
	bool Pause(char* error, size_t maxlen) override;
	bool Unpause(char* error, size_t maxlen) override;

	const char* GetAuthor() override { return PLUGIN_AUTHOR; }
	const char* GetName() override { return PLUGIN_DISPLAY_NAME; }
	const char* GetDescription() override { return PLUGIN_DESCRIPTION; }
	const char* GetURL() override { return PLUGIN_URL; }
	const char* GetLicense() override { return PLUGIN_LICENSE; }
	const char* GetVersion() override { return PLUGIN_FULL_VERSION; }
	const char* GetDate() override { return __DATE__; }
	const char* GetLogTag() override { return PLUGIN_LOGTAG; }

	void OnLevelInit(char const* mapName, char const* mapEntities, char const* oldLevel, char const* landmarkName, bool loadGame,
					 bool background) override;
	void OnLevelShutdown() override;
	void OnGameFrame(bool simulating);
	void OnClientPutInServer(CPlayerSlot slot, const char* name, int type, uint64 xuid);
	void OnClientActive(CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid);
	void OnClientFullyConnect(CPlayerSlot slot);
	void OnClientDisconnect(CPlayerSlot slot, const char* name, uint64 xuid);
	void OnClientCommand(CPlayerSlot slot, const CCommand& command);
	void OnClientSettingsChanged(CPlayerSlot slot);

	void FireGameEvent(IGameEvent* event) override;

	bool IsLoaded() const { return loaded; }
	const PlayerInfo* FindPlayer(CPlayerSlot slot) const;
	PlayerInfo* FindPlayerMutable(CPlayerSlot slot);
#ifdef OW_ENABLE_CACHE_FALLBACK
	ow::persistence::ICacheStore* GetCacheStore() const { return cacheStore.get(); }
	ow::persistence::AsyncCacheWarmer* GetCacheWarmer() const { return cacheWarmer.get(); }
#endif
	ow::adapters::PostgreSQLAdapter* GetPersistenceAdapter() const { return persistenceAdapter.get(); }
	const ow::core::ClusterConfig* GetClusterConfig() const { return clusterConfig ? &*clusterConfig : nullptr; }
	ow::core::ModuleManager& GetModuleManager() { return moduleManager; }
	ow::core::EventBus& GetEventBus() { return eventBus; }
	ow::core::ServiceRegistry& GetServiceRegistry() { return serviceRegistry; }

private:
	void InstallHooks();
	void RemoveHooks();
	void InstallGameEvents();
	void RemoveGameEvents();
	void RefreshSteamAuthorization(PlayerInfo& player);
	void ResetPlayerSettings(PlayerInfo& player);
	void RequestPlayerSettings(PlayerInfo& player);
	void ProcessPlayerSettingsResults();
	void HandleTransferChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandlePlayerSpawned(const char* eventName);

	ISource2Server* server {};
	IVEngineServer2* engine {};
	ISource2GameClients* gameClients {};
	IGameEventManager2* gameEventManager {};
	bool loaded {};
	bool paused {};
	bool serverHookInstalled {};
	bool clientHooksInstalled {};
	bool gameEventsRegistered {};
	std::optional<ow::core::ClusterConfig> clusterConfig;
	std::unique_ptr<ow::adapters::PostgreSQLAdapter> persistenceAdapter;
	std::unique_ptr<ow::persistence::PlayerSettingsWorker> playerSettingsWorker;
#ifdef OW_ENABLE_CACHE_FALLBACK
	std::unique_ptr<ow::persistence::ICacheStore> cacheStore;
	std::unique_ptr<ow::persistence::AsyncCacheWarmer> cacheWarmer;
#endif
	ow::core::ModuleManager moduleManager;
	ow::core::EventBus eventBus;
	ow::core::ServiceRegistry serviceRegistry;
	std::unique_ptr<ow::modules::CoreModule> coreModule;
	std::unique_ptr<ow::modules::CharacterManager> characterManager;
	std::unique_ptr<ow::modules::TransferModule> transferModule;
	std::unordered_map<int, PlayerInfo> players;
	std::uint64_t nextSettingsRequestId {};
};

extern OpenWorldPlugin g_OpenWorld;
