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
			return index;
		}
	}
	hoveredItem.reset();
	return std::nullopt;
}

void ParticleMenu::SetHoveredItem(std::optional<std::size_t> itemIndex)
{
	if (!IsOpen() || (itemIndex && (*itemIndex >= ItemsPerPage || !pages[pageIndex][*itemIndex].enabled)))
	{
		hoveredItem.reset();
		return;
	}
	hoveredItem = itemIndex;
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
	return true;
}

void ParticleMenu::Close()
{
	pages.clear();
	hoveredItem.reset();
	pageHistory.clear();
	pageIndex = 0;
	cursor = {};
}
} // namespace ow::ui::particle
