#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <string>

namespace ow::adapters
{
struct PlayerSettings
{
	std::uint64_t revision {};
	std::string settingsJson;
};

class PlayerSettingsCache
{
public:
	explicit PlayerSettingsCache(std::size_t maxEntries = 512, std::size_t maxBytes = 4 * 1024 * 1024,
								 std::chrono::milliseconds ttl = std::chrono::seconds(30));

	std::optional<PlayerSettings> Get(std::uint64_t steamId);
	bool Put(std::uint64_t steamId, PlayerSettings settings);
	bool Invalidate(std::uint64_t steamId);
	void Clear();
	std::size_t Size() const;
	std::size_t BytesInUse() const;
	// False when the cache was constructed with unusable limits; every operation then fails safely.
	bool IsEnabled() const noexcept;

private:
	struct Entry
	{
		PlayerSettings settings;
		std::chrono::steady_clock::time_point expiresAt;
		std::size_t bytes {};
		std::list<std::uint64_t>::iterator lruPosition {};
	};

	void Remove(std::unordered_map<std::uint64_t, Entry>::iterator entry);
	void PurgeExpired(std::chrono::steady_clock::time_point now);
	void EvictLeastRecentlyUsed();

	mutable std::mutex mutex;
	std::unordered_map<std::uint64_t, Entry> entries;
	std::list<std::uint64_t> leastRecentlyUsed;
	const std::size_t maxEntries;
	const std::size_t maxBytes;
	const std::chrono::milliseconds ttl;
	std::size_t bytesInUse {};
	bool enabled {true};
};
} // namespace ow::adapters
