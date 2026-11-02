#include "map_overview_cache.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace ow::ui::particle
{
namespace
{
constexpr std::uintmax_t MaxOverviewBytes = 32ull * 1024ull * 1024ull;
constexpr std::uintmax_t MaxMetadataBytes = 4096;
constexpr std::uint32_t MaxOverviewDimension = 8192;
constexpr std::uint64_t MaxOverviewPixels = 64ull * 1024ull * 1024ull;
constexpr std::array<unsigned char, 8> PngSignature {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

struct PngInfo
{
	std::uint32_t width {};
	std::uint32_t height {};
};

std::uint32_t ReadBigEndian32(const unsigned char* bytes)
{
	return (static_cast<std::uint32_t>(bytes[0]) << 24) |
		(static_cast<std::uint32_t>(bytes[1]) << 16) |
		(static_cast<std::uint32_t>(bytes[2]) << 8) |
		static_cast<std::uint32_t>(bytes[3]);
}

constexpr auto PngCrcTable = [] {
	std::array<std::uint32_t, 256> table {};
	for (std::uint32_t index = 0; index < table.size(); ++index)
	{
		std::uint32_t value = index;
		for (int bit = 0; bit < 8; ++bit)
		{
			value = (value & 1) ? (0xedb88320u ^ (value >> 1)) : (value >> 1);
		}
		table[index] = value;
	}
	return table;
}();

std::uint32_t UpdatePngCrc(std::uint32_t crc, const unsigned char* bytes, std::size_t count)
{
	for (std::size_t index = 0; index < count; ++index)
	{
		crc = PngCrcTable[(crc ^ bytes[index]) & 0xff] ^ (crc >> 8);
	}
	return crc;
}

bool InspectPng(const std::filesystem::path& path, PngInfo& info, std::string& error)
{
	std::error_code filesystemError;
	const std::uintmax_t fileSize = std::filesystem::file_size(path, filesystemError);
	if (filesystemError || fileSize < 57 || fileSize > MaxOverviewBytes)
	{
		error = "PNG is missing, truncated, or larger than the 32 MiB limit.";
		return false;
	}

	std::ifstream input(path, std::ios::binary);
	std::array<unsigned char, PngSignature.size()> signature {};
	if (!input.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size())) ||
		signature != PngSignature)
	{
		error = "Overview does not have a PNG signature.";
		return false;
	}

	bool hasHeader = false;
	bool hasImageData = false;
	bool hasEnd = false;
	std::uintmax_t offset = PngSignature.size();
	std::array<unsigned char, 8192> dataBuffer {};
	while (offset < fileSize)
	{
		if (fileSize - offset < 12)
		{
			error = "Overview PNG ends inside a chunk.";
			return false;
		}

		std::array<unsigned char, 8> chunkHeader {};
		if (!input.read(reinterpret_cast<char*>(chunkHeader.data()), static_cast<std::streamsize>(chunkHeader.size())))
		{
			error = "Could not read an overview PNG chunk header.";
			return false;
		}
		const std::uint32_t chunkLength = ReadBigEndian32(chunkHeader.data());
		if (chunkLength > fileSize - offset - 12)
		{
			error = "Overview PNG contains a truncated chunk.";
			return false;
		}

		const unsigned char* chunkType = chunkHeader.data() + 4;
		const bool isHeaderChunk = chunkType[0] == 'I' && chunkType[1] == 'H' && chunkType[2] == 'D' && chunkType[3] == 'R';
		const bool isDataChunk = chunkType[0] == 'I' && chunkType[1] == 'D' && chunkType[2] == 'A' && chunkType[3] == 'T';
		const bool isEndChunk = chunkType[0] == 'I' && chunkType[1] == 'E' && chunkType[2] == 'N' && chunkType[3] == 'D';
		if ((!hasHeader && (!isHeaderChunk || chunkLength != 13)) || (hasHeader && isHeaderChunk) ||
			(isEndChunk && (chunkLength != 0 || !hasImageData)))
		{
			error = "Overview PNG has an invalid chunk sequence.";
			return false;
		}

		std::uint32_t crc = UpdatePngCrc(0xffffffffu, chunkType, 4);
		if (isHeaderChunk)
		{
			std::array<unsigned char, 13> headerData {};
			if (!input.read(reinterpret_cast<char*>(headerData.data()), static_cast<std::streamsize>(headerData.size())))
			{
				error = "Could not read the overview PNG IHDR chunk.";
				return false;
			}
			crc = UpdatePngCrc(crc, headerData.data(), headerData.size());
			info.width = ReadBigEndian32(headerData.data());
			info.height = ReadBigEndian32(headerData.data() + 4);
			const unsigned char bitDepth = headerData[8];
			const unsigned char colorType = headerData[9];
			const bool validDepth = (colorType == 0 && (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16)) ||
				(colorType == 2 && (bitDepth == 8 || bitDepth == 16)) ||
				(colorType == 3 && (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8)) ||
				((colorType == 4 || colorType == 6) && (bitDepth == 8 || bitDepth == 16));
			if (!validDepth || headerData[10] != 0 || headerData[11] != 0 || headerData[12] > 1)
			{
				error = "Overview PNG uses unsupported IHDR settings.";
				return false;
			}
			hasHeader = true;
		}
		else
		{
			std::uint32_t bytesRemaining = chunkLength;
			while (bytesRemaining > 0)
			{
				const std::size_t count = std::min<std::size_t>(dataBuffer.size(), bytesRemaining);
				if (!input.read(reinterpret_cast<char*>(dataBuffer.data()), static_cast<std::streamsize>(count)))
				{
					error = "Could not read overview PNG chunk data.";
					return false;
				}
				crc = UpdatePngCrc(crc, dataBuffer.data(), count);
				bytesRemaining -= static_cast<std::uint32_t>(count);
			}
			if (isDataChunk && chunkLength > 0)
			{
				hasImageData = true;
			}
		}

		std::array<unsigned char, 4> crcBytes {};
		if (!input.read(reinterpret_cast<char*>(crcBytes.data()), static_cast<std::streamsize>(crcBytes.size())) ||
			(ReadBigEndian32(crcBytes.data()) != (crc ^ 0xffffffffu)))
		{
			error = "Overview PNG contains a chunk with an invalid CRC.";
			return false;
		}
		offset += static_cast<std::uintmax_t>(chunkLength) + 12;
		if (isEndChunk)
		{
			hasEnd = true;
			if (offset != fileSize)
			{
				error = "Overview PNG contains data after IEND.";
				return false;
			}
		}
	}

	const std::uint64_t pixelCount = static_cast<std::uint64_t>(info.width) * info.height;
	if (!hasHeader || !hasImageData || !hasEnd || info.width == 0 || info.height == 0 ||
		info.width > MaxOverviewDimension || info.height > MaxOverviewDimension || pixelCount > MaxOverviewPixels)
	{
		error = "Overview PNG is incomplete or its dimensions are outside the supported range.";
		return false;
	}
	return true;
}

std::string_view Trim(std::string_view value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
	{
		value.remove_prefix(1);
	}
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
	{
		value.remove_suffix(1);
	}
	return value;
}

bool ParseFiniteDouble(std::string_view value, double& parsed)
{
	const std::string ownedValue(Trim(value));
	if (ownedValue.empty())
	{
		return false;
	}

	const char* begin = ownedValue.data();
	const char* end = begin + ownedValue.size();
	const auto conversion = std::from_chars(begin, end, parsed, std::chars_format::general);
	return conversion.ec == std::errc{} && conversion.ptr == end && std::isfinite(parsed);
}

bool ReadBounds(const std::filesystem::path& path, MapOverviewBounds& bounds, bool& exists, std::string& error)
{
	std::error_code filesystemError;
	exists = std::filesystem::exists(path, filesystemError);
	if (filesystemError)
	{
		error = "Could not inspect map overview metadata.";
		return false;
	}
	if (!exists)
	{
		return false;
	}

	const std::uintmax_t fileSize = std::filesystem::file_size(path, filesystemError);
	if (filesystemError || fileSize > MaxMetadataBytes)
	{
		error = "Map overview metadata is unreadable or larger than 4 KiB.";
		return false;
	}

	std::ifstream input(path);
	if (!input)
	{
		error = "Could not open map overview metadata.";
		return false;
	}

	bool hasMinX = false;
	bool hasMinY = false;
	bool hasMaxX = false;
	bool hasMaxY = false;
	std::string line;
	while (std::getline(input, line))
	{
		const std::string_view trimmed = Trim(line);
		if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';')
		{
			continue;
		}

		const std::size_t separator = trimmed.find('=');
		if (separator == std::string_view::npos)
		{
			error = "Map overview metadata contains a line without '='.";
			return false;
		}

		const std::string_view key = Trim(trimmed.substr(0, separator));
		const std::string_view value = Trim(trimmed.substr(separator + 1));
		double parsed = 0.0;
		if (!ParseFiniteDouble(value, parsed))
		{
			error = "Map overview metadata contains an invalid coordinate.";
			return false;
		}

		bool* present = nullptr;
		double* destination = nullptr;
		if (key == "min_x")
		{
			present = &hasMinX;
			destination = &bounds.minX;
		}
		else if (key == "min_y")
		{
			present = &hasMinY;
			destination = &bounds.minY;
		}
		else if (key == "max_x")
		{
			present = &hasMaxX;
			destination = &bounds.maxX;
		}
		else if (key == "max_y")
		{
			present = &hasMaxY;
			destination = &bounds.maxY;
		}
		else
		{
			error = "Map overview metadata contains an unknown key.";
			return false;
		}

		if (*present)
		{
			error = "Map overview metadata contains a duplicate key.";
			return false;
		}
		*present = true;
		*destination = parsed;
	}

	if (!input.eof() || !hasMinX || !hasMinY || !hasMaxX || !hasMaxY || !bounds.IsValid())
	{
		error = "Map overview metadata must provide finite min_x, min_y, max_x, and max_y bounds.";
		return false;
	}
	return true;
}

bool CopyAtomically(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error)
{
	std::error_code filesystemError;
	std::filesystem::create_directories(destination.parent_path(), filesystemError);
	if (filesystemError)
	{
		error = "Could not create the map overview cache directory.";
		return false;
	}

	std::filesystem::path temporaryPath = destination;
	temporaryPath += ".tmp";
	std::filesystem::remove(temporaryPath, filesystemError);
	filesystemError.clear();
	std::filesystem::copy_file(source, temporaryPath, std::filesystem::copy_options::overwrite_existing, filesystemError);
	if (filesystemError)
	{
		error = "Could not copy the map overview into the cache.";
		return false;
	}

	filesystemError.clear();
	std::filesystem::remove(destination, filesystemError);
	if (filesystemError)
	{
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		error = "Could not replace an invalid cached map overview.";
		return false;
	}

	filesystemError.clear();
	std::filesystem::rename(temporaryPath, destination, filesystemError);
	if (filesystemError)
	{
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		error = "Could not finalize the cached map overview.";
		return false;
	}
	return true;
}
} // namespace

bool MapOverviewBounds::IsValid() const
{
	if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY) ||
		maxX <= minX || maxY <= minY)
	{
		return false;
	}
	return std::isfinite(maxX - minX) && std::isfinite(maxY - minY);
}

std::optional<MapOverviewPoint> MapOverviewBounds::Project(double worldX, double worldY) const
{
	if (!IsValid() || !std::isfinite(worldX) || !std::isfinite(worldY) ||
		worldX < minX || worldX > maxX || worldY < minY || worldY > maxY)
	{
		return std::nullopt;
	}

	return MapOverviewPoint {
		(worldX - minX) / (maxX - minX),
		(maxY - worldY) / (maxY - minY),
	};
}

MapOverviewCache::MapOverviewCache(std::filesystem::path mapAssetsDirectory, std::filesystem::path cacheDirectory)
	: mapAssetsDirectory(std::move(mapAssetsDirectory)), cacheDirectory(std::move(cacheDirectory))
{
}

void MapOverviewCache::SetDirectories(std::filesystem::path assetsDirectory, std::filesystem::path targetCacheDirectory)
{
	mapAssetsDirectory = std::move(assetsDirectory);
	cacheDirectory = std::move(targetCacheDirectory);
}

std::optional<std::string> MapOverviewCache::NormalizeMapName(std::string_view mapName)
{
	if (mapName.empty() || mapName.size() > 96)
	{
		return std::nullopt;
	}

	std::string normalized;
	normalized.reserve(mapName.size());
	for (const unsigned char character : mapName)
	{
		if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
			character == '_' || character == '-')
		{
			normalized.push_back(static_cast<char>(character));
		}
		else if (character >= 'A' && character <= 'Z')
		{
			normalized.push_back(static_cast<char>(character - 'A' + 'a'));
		}
		else if (character == '/' || character == 92)
		{
			if (normalized.empty() || normalized.back() != '_')
			{
				normalized.push_back('_');
			}
		}
		else
		{
			return std::nullopt;
		}
	}
	if (normalized.empty() || normalized.front() == '_' || normalized.back() == '_')
	{
		return std::nullopt;
	}
	return normalized;
}

MapOverviewLoadResult MapOverviewCache::Load(std::string_view mapName) const
{
	MapOverviewLoadResult result;
	const auto normalizedName = NormalizeMapName(mapName);
	if (!normalizedName)
	{
		result.error = "Map name contains unsupported characters.";
		return result;
	}
	if (mapAssetsDirectory.empty() || cacheDirectory.empty())
	{
		result.error = "Map overview directories have not been configured.";
		return result;
	}

	const std::string overviewFileName = *normalizedName + "_overview.png";
	const std::filesystem::path sourceImage = mapAssetsDirectory / overviewFileName;
	const std::filesystem::path cachedImage = cacheDirectory / overviewFileName;
	PngInfo imageInfo;
	std::string imageError;

	if (!InspectPng(cachedImage, imageInfo, imageError))
	{
		PngInfo sourceInfo;
		std::string sourceError;
		if (!InspectPng(sourceImage, sourceInfo, sourceError))
		{
			result.error = "No valid map overview PNG is available in the assets or cache directory.";
			return result;
		}
		if (!CopyAtomically(sourceImage, cachedImage, result.error))
		{
			return result;
		}
		if (!InspectPng(cachedImage, imageInfo, result.error))
		{
			return result;
		}
	}

	MapOverviewAsset asset;
	asset.mapName = *normalizedName;
	asset.imagePath = cachedImage;
	asset.width = imageInfo.width;
	asset.height = imageInfo.height;

	const std::filesystem::path sourceMetadata = mapAssetsDirectory / (*normalizedName + ".overview.ini");
	const std::filesystem::path cachedMetadata = cacheDirectory / (*normalizedName + ".overview.ini");
	MapOverviewBounds bounds;
	bool metadataExists = false;
	std::string metadataError;
	if (ReadBounds(cachedMetadata, bounds, metadataExists, metadataError))
	{
		asset.worldBounds = bounds;
	}
	else
	{
		MapOverviewBounds sourceBounds;
		bool sourceMetadataExists = false;
		std::string sourceMetadataError;
		if (ReadBounds(sourceMetadata, sourceBounds, sourceMetadataExists, sourceMetadataError))
		{
			asset.worldBounds = sourceBounds;
			std::string copyWarning;
			if (!CopyAtomically(sourceMetadata, cachedMetadata, copyWarning) && result.warning.empty())
			{
				result.warning = std::move(copyWarning);
			}
		}
		else if (metadataExists || sourceMetadataExists)
		{
			result.warning = !metadataError.empty() ? std::move(metadataError) : std::move(sourceMetadataError);
		}
	}

	result.overview = std::move(asset);
	return result;
}
} // namespace ow::ui::particle
