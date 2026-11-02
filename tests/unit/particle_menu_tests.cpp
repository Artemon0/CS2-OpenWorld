#include "ui/particle/map_overview_cache.h"
#include "ui/particle/particle_menu.h"
#include "ui/particle/particle_menu_sessions.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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

	if (!menu.SetPages(pages) || !menu.IsOpen() || !menu.MoveHudSelection(1) ||
		menu.GetHudHoverIndex() != 1 || !menu.MoveHudSelection(-1) || menu.GetHudHoverIndex() != 0)
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

void AppendBigEndian32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
	bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
	bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
	bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	bytes.push_back(static_cast<unsigned char>(value & 0xff));
}

void AppendPngChunk(std::vector<unsigned char>& png, const char (&type)[5], const std::vector<unsigned char>& data)
{
	AppendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
	const std::size_t crcStart = png.size();
	for (std::size_t index = 0; index < 4; ++index)
	{
		png.push_back(static_cast<unsigned char>(type[index]));
	}
	png.insert(png.end(), data.begin(), data.end());

	std::uint32_t crc = 0xffffffffu;
	for (std::size_t index = crcStart; index < png.size(); ++index)
	{
		crc ^= png[index];
		for (int bit = 0; bit < 8; ++bit)
		{
			crc = (crc & 1) ? (0xedb88320u ^ (crc >> 1)) : (crc >> 1);
		}
	}
	AppendBigEndian32(png, crc ^ 0xffffffffu);
}

bool WriteTinyPng(const std::filesystem::path& path)
{
	std::vector<unsigned char> png {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	const std::vector<unsigned char> header {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0};
	const std::vector<unsigned char> imageData {
		0x78, 0x01, 0x01, 0x05, 0x00, 0xfa, 0xff,
		0, 0, 0, 0, 0, 0, 5, 0, 1,
	};
	AppendPngChunk(png, "IHDR", header);
	AppendPngChunk(png, "IDAT", imageData);
	AppendPngChunk(png, "IEND", {});

	std::ofstream output(path, std::ios::binary);
	return output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())).good();
}

bool RunMapOverviewTests()
{
	using namespace ow::ui::particle;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const std::filesystem::path testDirectory = std::filesystem::temp_directory_path() /
		("cs2-openworld-map-overview-" + std::to_string(suffix));
	const auto sourceDirectory = testDirectory / "maps";
	const auto cacheDirectory = testDirectory / "addons" / "cs2-openworld" / "cache" / "maps";
	std::error_code filesystemError;
	std::filesystem::create_directories(sourceDirectory, filesystemError);
	if (filesystemError || !WriteTinyPng(sourceDirectory / "test_map_overview.png"))
	{
		std::filesystem::remove_all(testDirectory, filesystemError);
		return false;
	}

	{
		std::ofstream metadata(sourceDirectory / "test_map.overview.ini");
		metadata << "# World XY bounds used to position map markers\n"
				 << "min_x = -100\nmin_y = 0\nmax_x = 100\nmax_y = 200\n";
		if (!metadata.good())
		{
			std::filesystem::remove_all(testDirectory, filesystemError);
			return false;
		}
	}

	MapOverviewCache cache(sourceDirectory, cacheDirectory);
	const auto loaded = cache.Load("Test_Map");
	if (!loaded || loaded.overview->mapName != "test_map" || loaded.overview->width != 1 ||
		loaded.overview->height != 1 || !std::filesystem::exists(loaded.overview->imagePath) ||
		!loaded.overview->worldBounds)
	{
		std::filesystem::remove_all(testDirectory, filesystemError);
		return false;
	}

	const auto center = loaded.overview->worldBounds->Project(0.0, 100.0);
	const auto topLeft = loaded.overview->worldBounds->Project(-100.0, 200.0);
	if (!center || std::abs(center->u - 0.5) > 0.0001 || std::abs(center->v - 0.5) > 0.0001 ||
		!topLeft || topLeft->u != 0.0 || topLeft->v != 0.0 ||
		loaded.overview->worldBounds->Project(101.0, 100.0))
	{
		std::filesystem::remove_all(testDirectory, filesystemError);
		return false;
	}

	std::filesystem::remove_all(sourceDirectory, filesystemError);
	MapOverviewCache cachedOnly(sourceDirectory, cacheDirectory);
	const auto loadedFromCache = cachedOnly.Load("test_map");
	const auto normalized = MapOverviewCache::NormalizeMapName("Test_Map");
	const auto workshopName = MapOverviewCache::NormalizeMapName("workshop/12345/test_map");
	const auto rejected = cachedOnly.Load("../outside");
	std::filesystem::remove_all(testDirectory, filesystemError);
	return loadedFromCache && loadedFromCache.overview->worldBounds.has_value() && normalized &&
		*normalized == "test_map" && workshopName && *workshopName == "workshop_12345_test_map" &&
		!rejected && !MapOverviewCache::NormalizeMapName("../outside");
}
} // namespace

int main()
{
	if (!RunMenuTests() || !RunSessionTests() || !RunMapOverviewTests())
	{
		std::fprintf(stderr, "Particle menu tests failed.\n");
		return 1;
	}
	return 0;
}
