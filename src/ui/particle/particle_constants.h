#pragma once

#include <cstddef>
#include <cstdint>

namespace ow::ui::particle
{
constexpr std::size_t ItemCount = 6;
constexpr std::size_t MaxPages = 64;

// Canvas & layout constants used by Source 2 particle shader
constexpr float HudCanvasHalfSize = 512.0f;
constexpr float HudFirstRowCenterTextureY = 301.0f;
constexpr float HudRowTextureSpacing = 90.0f;
constexpr float HudRowRendererSpacing = -(HudRowTextureSpacing / HudCanvasHalfSize); // -0.17578125f

constexpr float HudCursorRowMin = -0.5f;
constexpr float HudCursorRowMax = static_cast<float>(ItemCount) + 0.5f; // 6.5f

// Special hover target indices
constexpr int HoverTargetNone = -1;
constexpr int HoverTargetPreviousPage = static_cast<int>(ItemCount);     // 6
constexpr int HoverTargetNextPage = static_cast<int>(ItemCount) + 1;     // 7
constexpr int HoverTargetCloseMenu = static_cast<int>(ItemCount) + 2;    // 8

// Cursor X bounds for footer buttons
constexpr float PreviousPageCursorMaxX = -0.28f;
constexpr float NextPageCursorMinX = -0.24f;
constexpr float NextPageCursorMaxX = 0.24f;
constexpr float CloseMenuCursorMinX = 0.28f;

// Default HUD config constants
constexpr float DefaultHudItemX = 0.0f;
constexpr float DefaultHudPanelY = 0.10f;
constexpr float DefaultHudPanelScale = 0.44f;
constexpr float DefaultCursorSensitivityX = 0.042f;
constexpr float DefaultCursorRowsPerPitchDegree = 0.20f;
constexpr float DefaultCursorXLimit = 0.66f;

constexpr float DefaultCursorXStep = 0.004f;
constexpr float DefaultCursorRowStep = 0.02f;
constexpr std::int64_t DefaultCursorSyncIntervalMs = 15;
constexpr std::int64_t DefaultTransmitRefreshIntervalMs = 1000;
constexpr std::int64_t DefaultActivationDebounceMs = 180;

// Particle asset paths
inline constexpr const char* CustomShellParticle = "particles/swift_particle_menu/menu_shell.vpcf";
inline constexpr const char* CustomHighlightParticle = "particles/swift_particle_menu/menu_highlight.vpcf";
inline constexpr const char* CustomCursorParticle = "particles/swift_particle_menu/menu_cursor.vpcf";
inline constexpr const char* CustomRootParticle = "particles/swift_particle_menu/menu_hud_root.vpcf";
inline constexpr const char* CustomPageTotalParticle = "particles/swift_particle_menu/menu_page_total.vpcf";
inline constexpr const char* BuiltinItemParticle = "particles/ui/ui_mainmenu_nav_hover.vpcf";
inline constexpr const char* BuiltinHighlightParticle = "particles/ui/xpshop/xpshop_panel_select.vpcf";

// UI sounds
inline constexpr const char* OpenSound = "sounds/ui/buttonrollover.vsnd";
inline constexpr const char* SelectSound = "sounds/ui/buttonclick.vsnd";
inline constexpr const char* CloseSound = "sounds/ui/buttonclickrelease.vsnd";

struct HudParticleLayout
{
	float x {DefaultHudItemX};
	float y {DefaultHudPanelY};
	float scale {DefaultHudPanelScale};
	float colorR {255.0f};
	float colorG {255.0f};
	float colorB {255.0f};
	int selection {0};
	float cursorX {0.0f};
	float cursorY {0.0f};
	int page {0};
};

inline HudParticleLayout CreateCompositeHudLayout(
	int selection,
	int page = 0,
	float cursorX = DefaultHudItemX,
	float cursorRowPosition = 0.0f,
	float panelX = DefaultHudItemX,
	float panelY = DefaultHudPanelY,
	float panelScale = DefaultHudPanelScale)
{
	const float firstRowRendererY = panelY - ((HudFirstRowCenterTextureY - HudCanvasHalfSize) / HudCanvasHalfSize);
	const float cursorRendererY = firstRowRendererY + (cursorRowPosition * HudRowRendererSpacing);

	HudParticleLayout layout;
	layout.x = panelX;
	layout.y = panelY;
	layout.scale = panelScale;
	layout.colorR = 255.0f;
	layout.colorG = 255.0f;
	layout.colorB = 255.0f;
	layout.selection = selection;
	layout.cursorX = cursorX;
	layout.cursorY = cursorRendererY;
	layout.page = page;
	return layout;
}
} // namespace ow::ui::particle
