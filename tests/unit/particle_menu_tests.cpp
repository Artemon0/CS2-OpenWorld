#include "ui/particle/particle_menu.h"
#include "ui/particle/particle_menu_sessions.h"

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
	const MenuItemBounds bounds{0.6f, 0.55f, 0.8f, 0.65f};
	pages[0][0] = {"next", "Next page", true, 1, bounds};
	pages[0][1] = {"help", "Help", true, std::nullopt, {0.2f, 0.2f, 0.4f, 0.4f}};
	pages[1][0] = {"inventory", "Inventory", true, std::nullopt, bounds};

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

	menu.MoveCursor(-27.0f, 15.0f, 0.01f);
	if (menu.HitTestCursor() != 0)
	{
		return false;
	}

	menu.SetHoveredItem(0);
	if (menu.ActivateHovered().has_value() || menu.GetPageIndex() != 1)
	{
		return false;
	}
	if (!menu.NavigateBack() || menu.GetPageIndex() != 0 || menu.NavigateBack())
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

bool RunSessionTests()
{
	using namespace ow::ui::particle;
	ParticleMenuSessions sessions;
	std::vector<std::array<MenuItem, ParticleMenu::ItemsPerPage>> pages(1);
	pages[0][0] = {"confirm", "Confirm", true, std::nullopt, {0.4f, 0.4f, 0.6f, 0.6f}};

	if (sessions.Open(0, true, pages, 0.0f, 0.0f) ||
		sessions.Open(76561198000000000ull, false, pages, 0.0f, 0.0f) ||
		!sessions.Open(76561198000000000ull, true, pages, 0.0f, 0.0f))
	{
		return false;
	}

	if (!sessions.UpdateView(76561198000000000ull, 0.0f, 0.0f) ||
		sessions.Find(76561198000000000ull)->HitTestCursor() != 0)
	{
		return false;
	}
	const auto selection = sessions.HandleActivateInput(76561198000000000ull, true);
	if (!selection || selection->id != "confirm" || sessions.Find(1234) != nullptr)
	{
		return false;
	}
	if (sessions.HandleActivateInput(76561198000000000ull, true).has_value() ||
		sessions.HandleActivateInput(76561198000000000ull, false).has_value() ||
		!sessions.HandleActivateInput(76561198000000000ull, true).has_value())
	{
		return false;
	}
	return sessions.Close(76561198000000000ull) && sessions.Find(76561198000000000ull) == nullptr;
}
} // namespace

int main()
{
	if (!RunMenuTests() || !RunSessionTests())
	{
		std::fprintf(stderr, "Particle menu tests failed.\n");
		return 1;
	}
	return 0;
}
