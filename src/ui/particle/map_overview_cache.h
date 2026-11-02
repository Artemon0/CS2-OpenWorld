#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ow::ui::particle
{
struct MapOverviewPoint
{
	double u {};
	double v {};
};

struct MapOverviewBounds
{
	double minX {};
	double minY {};
	double maxX {};
	double maxY {};

	bool IsValid() const;
	std::optional<MapOverviewPoint> Project(double worldX, double worldY) const;
};

struct MapOverviewAsset
{
	std::string mapName;
	std::filesystem::path imagePath;
	std::uint32_t width {};
	std::uint32_t height {};
	std::optional<MapOverviewBounds> worldBounds;
};

struct MapOverviewLoadResult
{
	std::optional<MapOverviewAsset> overview;
	std::string error;
	std::string warning;

	explicit operator bool() const { return overview.has_value(); }
};

class MapOverviewCache
{
public:
	MapOverviewCache(std::filesystem::path mapAssetsDirectory = {}, std::filesystem::path cacheDirectory = {});

	void SetDirectories(std::filesystem::path mapAssetsDirectory, std::filesystem::path cacheDirectory);
	MapOverviewLoadResult Load(std::string_view mapName) const;

	static std::optional<std::string> NormalizeMapName(std::string_view mapName);

private:
	std::filesystem::path mapAssetsDirectory;
	std::filesystem::path cacheDirectory;
};
} // namespace ow::ui::particle
