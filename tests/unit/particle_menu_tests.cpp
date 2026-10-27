#include "ui/particle/particle_menu.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using namespace ow::ui::particle;

bool RunMenuTests()
{
	ParticleMenu menu;
	std::vector<std::array<MenuItem, ParticleMenu::ItemsPerPage>> pages(2);
	pages[0][0] = {"next", "Next page", true, 1};
	pages[0][1] = {"help", "Help", true, std::nullopt};
	pages[1][0] = {"inventory", "Inventory", true, std::nullopt};

	if (!menu.SetPages(pages) || !menu.IsOpen())
	{
		return false;
	}

	menu.MoveCursor(45.0f, -25.0f, 0.01f);
	const CursorPosition cursor = menu.GetCursorPosition();
	if (std::abs(cursor.x - 0.95f) > 0.001f || std::abs(cursor.y - 0.75f) > 0.001f)
	{
		return false;
	}

	menu.SetHoveredItem(0);
	if (menu.ActivateHovered().has_value() || menu.GetPageIndex() != 1)
	{
		return false;
	}

	menu.SetHoveredItem(0);
	const auto selection = menu.ActivateHovered();
	if (!selection || selection->id != "inventory" || selection->pageIndex != 1 || selection->itemIndex != 0)
	{
		return false;
	}

	menu.SetHoveredItem(ParticleMenu::ItemsPerPage);
	if (menu.GetHoveredItem().has_value())
	{
		return false;
	}

	menu.Close();
	return !menu.IsOpen() && menu.GetPageIndex() == 0;
}
} // namespace

int main()
{
	if (!RunMenuTests())
	{
		std::fprintf(stderr, "Particle menu tests failed.\n");
		return 1;
	}
	return 0;
}
