#pragma once

#include "particle_constants.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ow::ui::particle
{
struct MenuItemBounds
{
	float left {};
	float top {};
	float right {};
	float bottom {};
};

struct MenuItem
{
	std::string id;
	std::string label;
	bool enabled {};
	std::optional<std::size_t> targetPage;
	MenuItemBounds bounds;
};

struct MenuSelection
{
	std::string id;
	std::string label;
	std::size_t pageIndex {};
	std::size_t itemIndex {};
};

struct CursorPosition
{
	float x {0.5f};
	float y {0.5f};
};

class ParticleMenu
{
public:
	static constexpr std::size_t ItemsPerPage = ItemCount;
	static constexpr std::size_t MaximumPages = MaxPages;

	bool SetPages(std::vector<std::array<MenuItem, ItemsPerPage>> pages);
	void MoveCursor(float yawDelta, float pitchDelta, float sensitivity = 0.004f);
	std::optional<std::size_t> HitTestCursor();
	void SetHoveredItem(std::optional<std::size_t> itemIndex);
	std::optional<MenuSelection> ActivateHovered();
	bool NavigateBack();

	// Swiftly HUD overlay navigation & virtual cursor
	void MoveHudCursor(
		float yawDelta, float pitchDelta,
		float sensitivityX = DefaultCursorSensitivityX,
		float rowsPerPitchDegree = DefaultCursorRowsPerPitchDegree,
		float xLimit = DefaultCursorXLimit);
	int ComputeHudHover() const;
	bool MoveHudSelection(int delta);
	bool ChangePage(int delta);

	CursorPosition GetCursorPosition() const { return cursor; }
	float GetHudCursorX() const { return hudCursorX; }
	float GetHudCursorRow() const { return hudCursorRow; }
	int GetHudHoverIndex() const { return hudHoverIndex; }
	std::optional<std::size_t> GetHoveredItem() const { return hoveredItem; }
	std::size_t GetPageIndex() const { return pageIndex; }
	std::size_t GetPageCount() const { return pages.size(); }
	bool IsOpen() const { return !pages.empty(); }
	const std::vector<std::array<MenuItem, ItemsPerPage>>& GetPages() const { return pages; }
	void Close();

private:
	std::vector<std::array<MenuItem, ItemsPerPage>> pages;
	CursorPosition cursor;
	std::optional<std::size_t> hoveredItem;
	std::vector<std::size_t> pageHistory;
	std::size_t pageIndex {};

	float hudCursorX {DefaultHudItemX};
	float hudCursorRow {0.0f};
	int hudHoverIndex {HoverTargetNone};
};
} // namespace ow::ui::particle
