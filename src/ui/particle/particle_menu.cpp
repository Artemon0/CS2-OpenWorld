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
			if (item.targetPage && *item.targetPage >= newPages.size())
			{
				return false;
			}
		}
	}

	pages = std::move(newPages);
	pageIndex = 0;
	hoveredItem.reset();
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
}

void ParticleMenu::SetHoveredItem(std::optional<std::size_t> itemIndex)
{
	if (!IsOpen() || (itemIndex && *itemIndex >= ItemsPerPage))
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
		pageIndex = *item.targetPage;
		hoveredItem.reset();
		return std::nullopt;
	}

	return MenuSelection{item.id, item.label, pageIndex, itemIndex};
}

void ParticleMenu::Close()
{
	pages.clear();
	hoveredItem.reset();
	pageIndex = 0;
	cursor = {};
}
} // namespace ow::ui::particle
