#pragma once

#include <ISmmPlugin.h>
#include <eiface.h>
#include <igameevents.h>
#include <tier1/convar.h>
#include <playerslot.h>
#include <chrono>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "adapters/panorama/panorama_adapter.h"
#include "adapters/persistence/postgres_adapter.h"
#include "cluster_config.h"
#include "event_bus.h"
#include "module_manager.h"
#include "service_registry.h"
#include "version_gen.h"
#include "modules/persistence/player_settings_worker.h"
#include "modules/persistence/player_character_worker.h"
#include "modules/persistence/player_route_worker.h"
#ifdef OW_ENABLE_CACHE_FALLBACK
// The cache fallback members below need the complete ICacheStore/AsyncCacheWarmer types; without
// this include the opt-in build fails to compile.
#include "modules/persistence/cache_manager.h"
#endif
#include "modules/core/core_module.h"
#include "modules/character/character_manager.h"
#include "modules/transfer/transfer_module.h"
#include "modules/economy/economy_system.h"
#include "modules/inventory/inventory_system.h"
#include "ui/hud/hud_system.h"
#include "ui/input/input_manager.h"
#include "ui/particle/map_overview_cache.h"
#include "ui/ui_manager.h"

PLUGIN_GLOBALVARS();

enum class PlayerSettingsLoadStatus
{
	NotRequested,
	Loading,
	Ready,
	Failed
};

enum class PlayerCharacterStatus
{
	NotRequested,
	Loading,
	NeedsCreation,
	SelectionRequired,
	Ready,
	Failed
};

enum class PlayerServerRouteStatus
{
	NotRequested,
	Loading,
	Ready,
	Redirecting,
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
		std::uint64_t routeRequestId {};
		PlayerServerRouteStatus routeStatus {PlayerServerRouteStatus::NotRequested};
		ow::persistence::PlayerRouteOperation routeOperation {ow::persistence::PlayerRouteOperation::Resolve};
		std::string routeServerId;
		std::chrono::steady_clock::time_point nextZoneTransferCheck {};
		bool zoneTransferArmed {};
		std::uint64_t settingsRequestId {};
		PlayerSettingsLoadStatus settingsLoadStatus {PlayerSettingsLoadStatus::NotRequested};
		std::optional<ow::adapters::PlayerSettings> settings;
		std::string settingsLoadError;
		std::uint64_t characterRequestId {};
		PlayerCharacterStatus characterStatus {PlayerCharacterStatus::NotRequested};
		std::vector<ow::adapters::CharacterSummary> characters;
		std::optional<ow::adapters::CharacterData> character;
		std::string characterLoadError;
		std::string characterId;
	};

	OpenWorldPlugin();

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
	bool OnClientCommand(CPlayerSlot slot, const CCommand& command);
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
	ow::ui::UIManager& GetUIManager() { return uiManager; }
	ow::ui::HUDSystem& GetHUDSystem() { return hudSystem; }
	ow::ui::InputManager& GetInputManager() { return inputManager; }

private:
	bool IsGameplayEnabled() const;
	void InstallHooks();
	void RemoveHooks();
	void InstallGameEvents();
	void RemoveGameEvents();
	void RefreshSteamAuthorization(PlayerInfo& player);
	void ResetPlayerRoute(PlayerInfo& player);
	std::uint64_t NextPlayerRouteRequestId();
	void RequestPlayerRoute(PlayerInfo& player);
	bool RequestPlayerRouteAssignment(PlayerInfo& player, std::string_view destinationServerId);
	void ProcessPlayerRouteResults();
	void EnforcePlayerRoute(PlayerInfo& player);
	void CheckAutomaticZoneTransfer(PlayerInfo& player);
	void ResetPlayerSettings(PlayerInfo& player);
	void RequestPlayerSettings(PlayerInfo& player);
	void ProcessPlayerSettingsResults();
	void ResetPlayerCharacter(PlayerInfo& player);
	std::uint64_t NextCharacterRequestId();
	void RequestPlayerCharacters(PlayerInfo& player);
	void RequestCharacterCreate(PlayerInfo& player, std::string_view displayName);
	void RequestCharacterLoad(PlayerInfo& player, std::string_view characterId);
	void ProcessPlayerCharacterResults();
	void HandleCharacterChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleInventoryChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleMapChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleOpenInventory(CPlayerSlot slot);
	void HandleOpenMap(CPlayerSlot slot);
	void HandleInputAction(CPlayerSlot slot, ow::ui::InputAction action);
	void HandleBalanceChatCommand(CPlayerSlot slot, const CCommand& command);
	void SendPlayerText(CPlayerSlot slot, ow::ui::UITextChannel channel, std::string_view text) const;
	void HandleTransferChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleHudChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleMenuChatCommand(CPlayerSlot slot, const CCommand& command);
	void HandleMenuAction(CPlayerSlot slot, std::string_view actionId, std::string_view label);
	void HandlePlayerSpawned(CPlayerSlot slot);

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
	std::unique_ptr<ow::persistence::PlayerRouteWorker> playerRouteWorker;
	std::unique_ptr<ow::persistence::PlayerSettingsWorker> playerSettingsWorker;
	std::unique_ptr<ow::persistence::PlayerCharacterWorker> playerCharacterWorker;
#ifdef OW_ENABLE_CACHE_FALLBACK
	std::unique_ptr<ow::persistence::ICacheStore> cacheStore;
	std::unique_ptr<ow::persistence::AsyncCacheWarmer> cacheWarmer;
#endif
	ow::core::ModuleManager moduleManager;
	ow::core::EventBus eventBus;
	ow::core::ServiceRegistry serviceRegistry;
	ow::adapters::PanoramaAdapter panoramaAdapter;
	ow::ui::InputManager inputManager;
	ow::ui::UIManager uiManager;
	ow::ui::HUDSystem hudSystem;
	std::unique_ptr<ow::modules::CoreModule> coreModule;
	std::unique_ptr<ow::modules::CharacterManager> characterManager;
	std::unique_ptr<ow::economy::EconomySystem> economySystem;
	std::unique_ptr<ow::inventory::InventorySystem> inventorySystem;
	std::unique_ptr<ow::modules::TransferModule> transferModule;
	std::unordered_map<int, PlayerInfo> players;
	std::string currentMapName;
	ow::ui::particle::MapOverviewCache mapOverviewCache;
	std::optional<ow::ui::particle::MapOverviewAsset> currentMapOverview;
	std::uint64_t nextSettingsRequestId {};
	std::uint64_t nextPlayerRouteRequestId {};
	std::uint64_t nextCharacterRequestId {};
};

extern OpenWorldPlugin g_OpenWorld;
