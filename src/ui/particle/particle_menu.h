#pragma once

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
	static constexpr std::size_t ItemsPerPage = 6;
	static constexpr std::size_t MaximumPages = 64;

	bool SetPages(std::vector<std::array<MenuItem, ItemsPerPage>> pages);
	void MoveCursor(float yawDelta, float pitchDelta, float sensitivity = 0.004f);
	std::optional<std::size_t> HitTestCursor();
	void SetHoveredItem(std::optional<std::size_t> itemIndex);
	std::optional<MenuSelection> ActivateHovered();
	bool NavigateBack();

	CursorPosition GetCursorPosition() const { return cursor; }
	std::optional<std::size_t> GetHoveredItem() const { return hoveredItem; }
	std::size_t GetPageIndex() const { return pageIndex; }
	bool IsOpen() const { return !pages.empty(); }
	void Close();

private:
	std::vector<std::array<MenuItem, ItemsPerPage>> pages;
	CursorPosition cursor;
	std::optional<std::size_t> hoveredItem;
	std::vector<std::size_t> pageHistory;
	std::size_t pageIndex {};
};
} // namespace ow::ui::particle
