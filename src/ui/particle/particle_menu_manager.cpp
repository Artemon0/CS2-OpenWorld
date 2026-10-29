#include "particle_menu_manager.h"

#include "entity_spawner.h"
#include "particle_transmit_filter.h"
#include "player/player.h"
#include "player/sdk/cinbuttonstate.h"
#include "player/sdk/services.h"
#include "utils/interfaces.h"
#include "utils/utils.h"

#include <cmath>

namespace ow::ui::particle
{
namespace
{
constexpr uint64_t IN_ATTACK_BIT = (1ull << 0);
constexpr uint64_t IN_JUMP_BIT = (1ull << 1);
constexpr uint64_t IN_DUCK_BIT = (1ull << 2);
constexpr uint64_t IN_FORWARD_BIT = (1ull << 3);
constexpr uint64_t IN_BACK_BIT = (1ull << 4);
constexpr uint64_t IN_USE_BIT = (1ull << 5);
constexpr uint64_t IN_RELOAD_BIT = (1ull << 13);

std::vector<std::array<MenuItem, ItemCount>> CreateDefaultMenuPages()
{
	std::vector<std::array<MenuItem, ItemCount>> pages(2);
	const MenuItemBounds defaultBounds{0.0f, 0.0f, 1.0f, 1.0f};

	// Page 1
	pages[0][0] = {"inventory", "Inventory", true, std::nullopt, defaultBounds};
	pages[0][1] = {"loadout", "Loadout", true, std::nullopt, defaultBounds};
	pages[0][2] = {"market", "Market", true, std::nullopt, defaultBounds};
	pages[0][3] = {"craft", "Craft", true, std::nullopt, defaultBounds};
	pages[0][4] = {"settings", "Settings", true, std::nullopt, defaultBounds};
	pages[0][5] = {"help", "Help", true, std::nullopt, defaultBounds};

	// Page 2
	pages[1][0] = {"profile", "Profile", true, std::nullopt, defaultBounds};
	pages[1][1] = {"collection", "Collection", true, std::nullopt, defaultBounds};
	pages[1][2] = {"favorites", "Favorites", true, std::nullopt, defaultBounds};
	pages[1][3] = {"trade_offers", "Trade Offers", true, std::nullopt, defaultBounds};
	pages[1][4] = {"history", "History", true, std::nullopt, defaultBounds};
	pages[1][5] = {"about", "About", true, std::nullopt, defaultBounds};

	return pages;
}
} // namespace

bool ParticleMenuManager::Initialize(ISource2GameEntities* pGameEntities)
{
	if (s_Initialized)
	{
		return true;
	}

	EntitySpawner::Initialize();
	if (pGameEntities)
	{
		ParticleTransmitFilter::InstallHook(pGameEntities);
	}

	s_Initialized = true;
	return true;
}

void ParticleMenuManager::Shutdown(ISource2GameEntities* pGameEntities)
{
	if (!s_Initialized)
	{
		return;
	}

	CloseAll("shutdown");
	if (pGameEntities)
	{
		ParticleTransmitFilter::RemoveHook(pGameEntities);
	}
	s_Initialized = false;
}

std::int64_t ParticleMenuManager::GetCurrentTimeMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

bool ParticleMenuManager::OpenMenu(
	CPlayerSlot slot,
	std::vector<std::array<MenuItem, ItemCount>> pages,
	MenuActionCallback callback)
{
	if (slot.Get() < 0 || slot.Get() >= MAXPLAYERS)
	{
		return false;
	}

	// Close any active session first
	if (IsMenuOpen(slot))
	{
		CloseMenu(slot, false, "reopen");
	}

	Player* player = g_pPlayerManager ? g_pPlayerManager->ToPlayer(slot) : nullptr;
	if (!player || !player->IsInGame())
	{
		return false;
	}

	CCSPlayerPawn* pawn = player->GetPlayerPawn();
	if (!pawn || !pawn->IsAlive())
	{
		return false;
	}

	const QAngle eyeAngles = pawn->m_angEyeAngles();
	const Vector origin = pawn->GetAbsOrigin();

	// Spawn the composite menu shell particle entity
	CParticleSystem* pShell = EntitySpawner::SpawnParticleSystem(
		CustomShellParticle, origin, eyeAngles, true);
	if (!pShell)
	{
		// Fallback to built-in fallback particle if custom compiled particle is missing
		pShell = EntitySpawner::SpawnParticleSystem(
			BuiltinItemParticle, origin, eyeAngles, true);
	}

	if (!pShell)
	{
		return false;
	}

	const int entityIndex = pShell->entindex();

	// Set initial particle HUD layout
	const HudParticleLayout initialLayout = CreateCompositeHudLayout(0, 0, DefaultHudItemX, 0.0f);
	pShell->ApplyHudLayout(initialLayout);

	// Register entity for per-player transmission filtering so only 'slot' sees it
	ParticleTransmitFilter::RegisterMenuEntity(entityIndex, slot.Get());

	ActiveMenuSession session;
	session.slot = slot;
	session.menu.SetPages(std::move(pages));
	session.pShellEntity = pShell;
	session.shellEntityIndex = entityIndex;
	session.lastViewAngles = eyeAngles;
	session.sentHudCursorX = DefaultHudItemX;
	session.sentHudCursorRow = 0.0f;
	session.sentSelection = 0;
	session.sentPage = 0;
	session.lastCursorSyncMs = GetCurrentTimeMs();
	session.nextActivationMs = session.lastCursorSyncMs + DefaultActivationDebounceMs;

	FreezePlayer(session, pawn);
	PlaySound(slot, OpenSound);

	s_Sessions.insert_or_assign(slot.Get(), std::move(session));
	return true;
}

bool ParticleMenuManager::OpenDefaultMenu(CPlayerSlot slot, MenuActionCallback callback)
{
	return OpenMenu(slot, CreateDefaultMenuPages(), std::move(callback));
}

bool ParticleMenuManager::CloseMenu(CPlayerSlot slot, bool notify, const char* reason)
{
	const auto found = s_Sessions.find(slot.Get());
	if (found == s_Sessions.end())
	{
		return false;
	}

	ActiveMenuSession& session = found->second;

	Player* player = g_pPlayerManager ? g_pPlayerManager->ToPlayer(slot) : nullptr;
	CCSPlayerPawn* pawn = player ? player->GetPlayerPawn() : nullptr;
	if (pawn)
	{
		UnfreezePlayer(session, pawn);
	}

	if (session.pShellEntity)
	{
		// Send client-side kill signal (CP17 = -1, 0, 0)
		session.pShellEntity->SendKillSignal();
		ParticleTransmitFilter::UnregisterMenuEntity(session.shellEntityIndex);
		EntitySpawner::RemoveEntity(session.pShellEntity);
		session.pShellEntity = nullptr;
	}

	PlaySound(slot, CloseSound);
	s_Sessions.erase(found);
	return true;
}

void ParticleMenuManager::CloseAll(const char* reason)
{
	std::vector<int> slots;
	slots.reserve(s_Sessions.size());
	for (const auto& [slot, _] : s_Sessions)
	{
		slots.push_back(slot);
	}

	for (const int slot : slots)
	{
		CloseMenu(CPlayerSlot(slot), false, reason);
	}
}

void ParticleMenuManager::OnGameFrame(bool simulating)
{
	if (s_Sessions.empty())
	{
		return;
	}

	std::vector<int> activeSlots;
	activeSlots.reserve(s_Sessions.size());
	for (const auto& [slot, _] : s_Sessions)
	{
		activeSlots.push_back(slot);
	}

	for (const int slot : activeSlots)
	{
		const auto it = s_Sessions.find(slot);
		if (it != s_Sessions.end())
		{
			TickSession(it->second);
		}
	}
}

void ParticleMenuManager::TickSession(ActiveMenuSession& session)
{
	Player* player = g_pPlayerManager ? g_pPlayerManager->ToPlayer(session.slot) : nullptr;
	if (!player || !player->IsInGame())
	{
		CloseMenu(session.slot, false, "disconnected");
		return;
	}

	CCSPlayerPawn* pawn = player->GetPlayerPawn();
	if (!pawn || !pawn->IsAlive())
	{
		CloseMenu(session.slot, false, "died");
		return;
	}

	UpdateSessionView(session, pawn);
	HandleButtons(session, pawn);
}

void ParticleMenuManager::UpdateSessionView(ActiveMenuSession& session, CCSPlayerPawn* pawn)
{
	const QAngle currentAngles = pawn->m_angEyeAngles();
	float yawDelta = currentAngles.y - session.lastViewAngles.y;
	float pitchDelta = currentAngles.x - session.lastViewAngles.x;
	session.lastViewAngles = currentAngles;

	// Normalize deltas
	while (yawDelta > 180.0f) yawDelta -= 360.0f;
	while (yawDelta < -180.0f) yawDelta += 360.0f;

	const int oldHover = session.menu.GetHudHoverIndex();
	session.menu.MoveHudCursor(yawDelta, pitchDelta);
	const int newHover = session.menu.GetHudHoverIndex();

	if (oldHover != newHover)
	{
		if (newHover != HoverTargetNone)
		{
			PlaySound(session.slot, OpenSound);
		}
		SyncLayout(session, false);
	}
	else
	{
		const std::int64_t now = GetCurrentTimeMs();
		const float cursorXDelta = std::abs(session.menu.GetHudCursorX() - session.sentHudCursorX);
		const float cursorRowDelta = std::abs(session.menu.GetHudCursorRow() - session.sentHudCursorRow);

		if ((cursorXDelta >= DefaultCursorXStep || cursorRowDelta >= DefaultCursorRowStep) &&
			(now - session.lastCursorSyncMs >= DefaultCursorSyncIntervalMs))
		{
			SyncLayout(session, false);
		}
	}
}

void ParticleMenuManager::HandleButtons(ActiveMenuSession& session, CCSPlayerPawn* pawn)
{
	auto* movementServices = pawn->m_pMovementServices();
	if (!movementServices)
	{
		return;
	}

	uint64_t currentButtons[3] {};
	movementServices->m_nButtons().GetButtons(currentButtons);

	const uint64_t newlyPressed0 = currentButtons[0] & ~session.lastButtons[0];
	session.lastButtons[0] = currentButtons[0];
	session.lastButtons[1] = currentButtons[1];
	session.lastButtons[2] = currentButtons[2];

	// IN_USE (E) or IN_ATTACK (Mouse1)
	if ((newlyPressed0 & IN_USE_BIT) || (newlyPressed0 & IN_ATTACK_BIT))
	{
		ActivateCurrentHover(session);
		return;
	}

	// IN_RELOAD (R) to close
	if (newlyPressed0 & IN_RELOAD_BIT)
	{
		CloseMenu(session.slot, true, "reload_button");
		return;
	}

	// Optional W / S navigation
	if (newlyPressed0 & IN_FORWARD_BIT)
	{
		// Move selection up
		session.menu.ChangePage(0);
	}
	else if (newlyPressed0 & IN_BACK_BIT)
	{
		// Move selection down
		session.menu.ChangePage(0);
	}
}

void ParticleMenuManager::ActivateCurrentHover(ActiveMenuSession& session)
{
	const std::int64_t now = GetCurrentTimeMs();
	if (now < session.nextActivationMs)
	{
		return;
	}
	session.nextActivationMs = now + DefaultActivationDebounceMs;

	const int hover = session.menu.GetHudHoverIndex();
	if (hover == HoverTargetPreviousPage)
	{
		if (session.menu.ChangePage(-1))
		{
			PlaySound(session.slot, SelectSound);
			SyncLayout(session, true);
		}
		return;
	}
	if (hover == HoverTargetNextPage)
	{
		if (session.menu.ChangePage(1))
		{
			PlaySound(session.slot, SelectSound);
			SyncLayout(session, true);
		}
		return;
	}
	if (hover == HoverTargetCloseMenu)
	{
		CloseMenu(session.slot, true, "close_button");
		return;
	}

	if (hover >= 0 && static_cast<std::size_t>(hover) < ItemCount)
	{
		const auto selection = session.menu.ActivateHovered();
		if (!selection)
		{
			// Item had targetPage, so menu navigated to submenu
			PlaySound(session.slot, SelectSound);
			SyncLayout(session, true);
		}
		else
		{
			// Leaf item activated
			PlaySound(session.slot, SelectSound);
			if (s_DefaultActionHandler)
			{
				s_DefaultActionHandler(session.slot, selection->id, selection->label);
			}

			// Close menu after leaf item selection
			CloseMenu(session.slot, false, "item_selected");
		}
	}
}

void ParticleMenuManager::SyncLayout(ActiveMenuSession& session, bool force)
{
	if (!session.pShellEntity)
	{
		return;
	}

	const int hover = session.menu.GetHudHoverIndex();
	const int selection = (hover >= 0 && hover <= HoverTargetCloseMenu) ? (hover + 1) : 0;
	const int page = static_cast<int>(session.menu.GetPageIndex());
	const float cursorX = session.menu.GetHudCursorX();
	const float cursorRow = session.menu.GetHudCursorRow();

	if (!force && selection == session.sentSelection && page == session.sentPage &&
		std::abs(cursorX - session.sentHudCursorX) < DefaultCursorXStep &&
		std::abs(cursorRow - session.sentHudCursorRow) < DefaultCursorRowStep)
	{
		return;
	}

	const HudParticleLayout layout = CreateCompositeHudLayout(
		selection, page, cursorX, cursorRow);
	session.pShellEntity->ApplyHudLayout(layout);

	session.sentSelection = selection;
	session.sentPage = page;
	session.sentHudCursorX = cursorX;
	session.sentHudCursorRow = cursorRow;
	session.lastCursorSyncMs = GetCurrentTimeMs();
}

void ParticleMenuManager::PlaySound(CPlayerSlot slot, const char* soundPath)
{
	if (!interfaces::pEngine || !soundPath || soundPath[0] == '\0')
	{
		return;
	}

	char command[256];
	snprintf(command, sizeof(command), "play %s\n", soundPath);
	interfaces::pEngine->ClientCommand(slot, command);
}

void ParticleMenuManager::FreezePlayer(ActiveMenuSession& session, CCSPlayerPawn* pawn)
{
	if (session.moveFrozen || !pawn)
	{
		return;
	}

	session.originalMoveType = pawn->m_MoveType();
	session.moveFrozen = true;
	pawn->SetMoveType(MOVETYPE_NONE);
}

void ParticleMenuManager::UnfreezePlayer(ActiveMenuSession& session, CCSPlayerPawn* pawn)
{
	if (!session.moveFrozen || !pawn)
	{
		return;
	}

	pawn->SetMoveType(session.originalMoveType);
	session.moveFrozen = false;
}

void ParticleMenuManager::OnClientDisconnect(CPlayerSlot slot)
{
	CloseMenu(slot, false, "disconnect");
}

bool ParticleMenuManager::HandleClientCommand(CPlayerSlot slot, const char* commandString)
{
	if (!commandString || commandString[0] == '\0')
	{
		return false;
	}

	if (strcmp(commandString, "!pmenu") == 0 || strcmp(commandString, "pmenu") == 0 ||
		strcmp(commandString, "swift_particle_menu") == 0)
	{
		if (IsMenuOpen(slot))
		{
			CloseMenu(slot, true, "command_toggle");
		}
		else
		{
			OpenDefaultMenu(slot);
		}
		return true;
	}

	if (strcmp(commandString, "!pmenu_close") == 0 || strcmp(commandString, "pmenu_close") == 0 ||
		strcmp(commandString, "swift_particle_menu_close") == 0)
	{
		if (IsMenuOpen(slot))
		{
			CloseMenu(slot, true, "command_close");
		}
		return true;
	}

	return false;
}

bool ParticleMenuManager::IsMenuOpen(CPlayerSlot slot)
{
	return s_Sessions.find(slot.Get()) != s_Sessions.end();
}

ActiveMenuSession* ParticleMenuManager::GetSession(CPlayerSlot slot)
{
	const auto it = s_Sessions.find(slot.Get());
	return it != s_Sessions.end() ? &it->second : nullptr;
}

void ParticleMenuManager::SetActionHandler(MenuActionCallback callback)
{
	s_DefaultActionHandler = std::move(callback);
}
} // namespace ow::ui::particle
