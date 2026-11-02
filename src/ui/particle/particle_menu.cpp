#include "particle_menu.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ow::ui::particle
{
bool ParticleMenu::SetPages(std::vector<std::array<MenuItem, ItemsPerPage>> newPages)
{
	if (newPages.empty() || newPages.size() > MaximumPages)
	{
		return false;
	}

	for (const auto& page : newPages)
	{
		for (const auto& item : page)
		{
			if (item.enabled && (item.id.empty() || item.label.empty()))
			{
				return false;
			}
			if (item.enabled && (!std::isfinite(item.bounds.left) || !std::isfinite(item.bounds.top) ||
								 !std::isfinite(item.bounds.right) || !std::isfinite(item.bounds.bottom) ||
								 item.bounds.left < 0.0f || item.bounds.top < 0.0f || item.bounds.right > 1.0f ||
								 item.bounds.bottom > 1.0f || item.bounds.left >= item.bounds.right ||
								 item.bounds.top >= item.bounds.bottom))
			{
				return false;
			}
			if (item.targetPage && *item.targetPage >= newPages.size())
			{
				return false;
			}
		}
	}

	pages = std::move(newPages);
	pageIndex = 0;
	hoveredItem.reset();
	pageHistory.clear();
	cursor = {};
	hudCursorX = DefaultHudItemX;
	hudCursorRow = 0.0f;
	hudHoverIndex = HoverTargetNone;
	return true;
}

void ParticleMenu::MoveCursor(float yawDelta, float pitchDelta, float sensitivity)
{
	if (!IsOpen() || !std::isfinite(yawDelta) || !std::isfinite(pitchDelta) || !std::isfinite(sensitivity) ||
		sensitivity <= 0.0f)
	{
		return;
	}

	if (yawDelta > 180.0f)
	{
		yawDelta -= 360.0f;
	}
	else if (yawDelta < -180.0f)
	{
		yawDelta += 360.0f;
	}

	cursor.x = std::clamp(cursor.x + yawDelta * sensitivity, 0.0f, 1.0f);
	cursor.y = std::clamp(cursor.y - pitchDelta * sensitivity, 0.0f, 1.0f);
	HitTestCursor();
}

void ParticleMenu::MoveHudCursor(
	float yawDelta, float pitchDelta,
	float sensitivityX, float rowsPerPitchDegree, float xLimit)
{
	if (!IsOpen() || !std::isfinite(yawDelta) || !std::isfinite(pitchDelta) ||
		!std::isfinite(sensitivityX) || sensitivityX <= 0.0f ||
		!std::isfinite(rowsPerPitchDegree) || rowsPerPitchDegree <= 0.0f ||
		!std::isfinite(xLimit) || xLimit <= 0.0f)
	{
		return;
	}

	if (yawDelta > 180.0f)
	{
		yawDelta -= 360.0f;
	}
	else if (yawDelta < -180.0f)
	{
		yawDelta += 360.0f;
	}

	// Clamp angular spikes from teleports
	yawDelta = std::clamp(yawDelta, -12.0f, 12.0f);
	pitchDelta = std::clamp(pitchDelta, -12.0f, 12.0f);

	hudCursorX = std::clamp(hudCursorX - (yawDelta * sensitivityX), -xLimit, xLimit);
	hudCursorRow = std::clamp(hudCursorRow + (pitchDelta * rowsPerPitchDegree), HudCursorRowMin, HudCursorRowMax);

	hudHoverIndex = ComputeHudHover();
	if (hudHoverIndex >= 0 && static_cast<std::size_t>(hudHoverIndex) < ItemsPerPage)
	{
		hoveredItem = static_cast<std::size_t>(hudHoverIndex);
	}
	else
	{
		hoveredItem.reset();
	}
}

int ParticleMenu::ComputeHudHover() const
{
	if (!IsOpen())
	{
		return HoverTargetNone;
	}

	if (std::abs(hudCursorX) > DefaultCursorXLimit)
	{
		return HoverTargetNone;
	}

	if (hudCursorRow < HudCursorRowMin || hudCursorRow > HudCursorRowMax)
	{
		return HoverTargetNone;
	}

	const int row = static_cast<int>(std::floor(hudCursorRow + 0.5f));
	if (row >= 0 && static_cast<std::size_t>(row) < ItemsPerPage)
	{
		return pages[pageIndex][row].enabled ? row : HoverTargetNone;
	}

	// Row 6 corresponds to footer buttons
	if (row == static_cast<int>(ItemCount))
	{
		if (hudCursorX <= PreviousPageCursorMaxX)
		{
			return pageIndex > 0 ? HoverTargetPreviousPage : HoverTargetNone;
		}
		if (hudCursorX >= NextPageCursorMinX && hudCursorX <= NextPageCursorMaxX)
		{
			return (pageIndex + 1 < pages.size()) ? HoverTargetNextPage : HoverTargetNone;
		}
		if (hudCursorX >= CloseMenuCursorMinX)
		{
			return HoverTargetCloseMenu;
		}
	}

	return HoverTargetNone;
}

bool ParticleMenu::MoveHudSelection(int delta)
{
	if (!IsOpen() || delta == 0)
	{
		return false;
	}

	const int lastItem = static_cast<int>(ItemsPerPage) - 1;
	const int currentItem = hudHoverIndex >= 0 && hudHoverIndex <= lastItem
		? hudHoverIndex
		: static_cast<int>(std::lround(std::clamp(hudCursorRow, 0.0f, static_cast<float>(lastItem))));
	const int direction = delta > 0 ? 1 : -1;
	for (int target = currentItem + direction; target >= 0 && target <= lastItem; target += direction)
	{
		if (pages[pageIndex][static_cast<std::size_t>(target)].enabled)
		{
			hudCursorRow = static_cast<float>(target);
			hudHoverIndex = target;
			hoveredItem = static_cast<std::size_t>(target);
			return true;
		}
	}
	return false;
}

bool ParticleMenu::ChangePage(int delta)
{
	if (!IsOpen() || delta == 0)
	{
		return false;
	}

	if (delta < 0)
	{
		const auto steps = static_cast<std::uint64_t>(-static_cast<std::int64_t>(delta));
		if (steps > pageIndex)
		{
			return false;
		}
		pageIndex -= static_cast<std::size_t>(steps);
	}
	else
	{
		const auto steps = static_cast<std::uint64_t>(delta);
		if (steps >= pages.size() - pageIndex)
		{
			return false;
		}
		pageIndex += static_cast<std::size_t>(steps);
	}

	hudHoverIndex = ComputeHudHover();
	if (hudHoverIndex >= 0 && static_cast<std::size_t>(hudHoverIndex) < ItemsPerPage)
	{
		hoveredItem = static_cast<std::size_t>(hudHoverIndex);
	}
	else
	{
		hoveredItem.reset();
	}
	return true;
}

std::optional<std::size_t> ParticleMenu::HitTestCursor()
{
	if (!IsOpen())
	{
		hoveredItem.reset();
		return std::nullopt;
	}

	const auto& page = pages[pageIndex];
	for (std::size_t index = 0; index < page.size(); ++index)
	{
		const MenuItem& item = page[index];
		if (item.enabled && cursor.x >= item.bounds.left && cursor.x < item.bounds.right &&
			cursor.y >= item.bounds.top && cursor.y < item.bounds.bottom)
		{
			hoveredItem = index;
			hudHoverIndex = static_cast<int>(index);
			return index;
		}
	}
	hoveredItem.reset();
	hudHoverIndex = HoverTargetNone;
	return std::nullopt;
}

void ParticleMenu::SetHoveredItem(std::optional<std::size_t> itemIndex)
{
	if (!IsOpen() || (itemIndex && (*itemIndex >= ItemsPerPage || !pages[pageIndex][*itemIndex].enabled)))
	{
		hoveredItem.reset();
		hudHoverIndex = HoverTargetNone;
		return;
	}
	hoveredItem = itemIndex;
	hudHoverIndex = itemIndex ? static_cast<int>(*itemIndex) : HoverTargetNone;
}

std::optional<MenuSelection> ParticleMenu::ActivateHovered()
{
	if (!IsOpen() || !hoveredItem)
	{
		return std::nullopt;
	}

	const auto itemIndex = *hoveredItem;
	const MenuItem& item = pages[pageIndex][itemIndex];
	if (!item.enabled)
	{
		return std::nullopt;
	}
	if (item.targetPage)
	{
		pageHistory.push_back(pageIndex);
		pageIndex = *item.targetPage;
		hoveredItem.reset();
		hudHoverIndex = HoverTargetNone;
		return std::nullopt;
	}

	return MenuSelection{item.id, item.label, pageIndex, itemIndex};
}

bool ParticleMenu::NavigateBack()
{
	if (!IsOpen() || pageHistory.empty())
	{
		return false;
	}
	pageIndex = pageHistory.back();
	pageHistory.pop_back();
	hoveredItem.reset();
	hudHoverIndex = HoverTargetNone;
	return true;
}

void ParticleMenu::Close()
{
	pages.clear();
	hoveredItem.reset();
	pageHistory.clear();
	pageIndex = 0;
	cursor = {};
	hudCursorX = DefaultHudItemX;
	hudCursorRow = 0.0f;
	hudHoverIndex = HoverTargetNone;
}
} // namespace ow::ui::particle
