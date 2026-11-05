#pragma once

#include "particle_constants.h"
#include "particle_menu.h"
#include "particle_system_entity.h"
#include "playerslot.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CCSPlayerPawn;

namespace ow::ui::particle
{
using MenuActionCallback = std::function<void(CPlayerSlot slot, const std::string& actionId, const std::string& label)>;

struct ActiveMenuSession
{
	CPlayerSlot slot {-1};
	ParticleMenu menu;
	CHandle<CBaseEntity> shellEntity;
	CHandle<CBaseEntity> frozenPawn;
	MenuActionCallback actionHandler;
	int shellEntityIndex {0};

	QAngle lastViewAngles {0.0f, 0.0f, 0.0f};
	float sentHudCursorX {0.0f};
	float sentHudCursorRow {0.0f};
	int sentSelection {0};
	int sentPage {0};

	std::int64_t lastCursorSyncMs {0};
	std::int64_t nextActivationMs {0};
	bool moveFrozen {false};
	MoveType_t originalMoveType {MOVETYPE_WALK};
	uint64 lastButtons[3] {0, 0, 0};
};

class ParticleMenuManager
{
public:
	static bool Initialize(ISource2GameEntities* pGameEntities);
	static void Shutdown(ISource2GameEntities* pGameEntities);

	static bool OpenMenu(
		CPlayerSlot slot,
		std::vector<std::array<MenuItem, ItemCount>> pages,
		MenuActionCallback callback = nullptr);

	static bool OpenDefaultMenu(CPlayerSlot slot, MenuActionCallback callback = nullptr);
	static bool CloseMenu(CPlayerSlot slot, bool notify = true, const char* reason = "command");
	static void CloseAll(const char* reason = "shutdown");

	static void OnGameFrame(bool simulating);
	static void OnClientDisconnect(CPlayerSlot slot);
	static bool HandleClientCommand(CPlayerSlot slot, const char* commandString);

	static bool IsMenuOpen(CPlayerSlot slot);
	static ActiveMenuSession* GetSession(CPlayerSlot slot);
	static void SetActionHandler(MenuActionCallback callback);

private:
	static void TickSession(ActiveMenuSession& session);
	static void UpdateSessionView(ActiveMenuSession& session, CCSPlayerPawn* pawn);
	static void HandleButtons(ActiveMenuSession& session, CCSPlayerPawn* pawn);
	static void ActivateCurrentHover(ActiveMenuSession& session);
	static void SyncLayout(ActiveMenuSession& session, bool force);
	static void PlaySound(CPlayerSlot slot, const char* soundPath);
	static void FreezePlayer(ActiveMenuSession& session, CCSPlayerPawn* pawn);
	static void UnfreezePlayer(ActiveMenuSession& session);
	static std::int64_t GetCurrentTimeMs();

	static inline std::unordered_map<int, ActiveMenuSession> s_Sessions;
	static inline MenuActionCallback s_DefaultActionHandler = nullptr;
	static inline bool s_Initialized = false;
};
} // namespace ow::ui::particle
