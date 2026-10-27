#pragma once

#include <ISmmPlugin.h>
#include <eiface.h>
#include <tier1/convar.h>
#include <playerslot.h>

#include <string>
#include <unordered_map>

#include "module_manager.h"
#include "version_gen.h"

PLUGIN_GLOBALVARS();

class OpenWorldPlugin final : public ISmmPlugin, public IMetamodListener
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

	bool IsLoaded() const { return loaded; }
	const PlayerInfo* FindPlayer(CPlayerSlot slot) const;

private:
	void InstallHooks();
	void RemoveHooks();
	void RefreshSteamAuthorization(PlayerInfo& player);

	ISource2Server* server {};
	IVEngineServer2* engine {};
	ISource2GameClients* gameClients {};
	bool loaded {};
	bool paused {};
	bool serverHookInstalled {};
	bool clientHooksInstalled {};
	ow::core::ModuleManager moduleManager;
	std::unordered_map<int, PlayerInfo> players;
};

extern OpenWorldPlugin g_OpenWorld;
