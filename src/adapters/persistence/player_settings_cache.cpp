#include "player_settings_cache.h"

#include <stdexcept>
#include <utility>

namespace ow::adapters
{
PlayerSettingsCache::PlayerSettingsCache(std::size_t maxEntries, std::size_t maxBytes, std::chrono::milliseconds ttl)
	: maxEntries(maxEntries), maxBytes(maxBytes), ttl(ttl)
{
	if (maxEntries == 0 || maxBytes == 0 || ttl <= std::chrono::milliseconds::zero())
	{
		throw std::invalid_argument("Player settings cache limits and TTL must be positive.");
	}
}

std::optional<PlayerSettings> PlayerSettingsCache::Get(std::uint64_t steamId)
{
	if (steamId == 0)
	{
		return std::nullopt;
	}

	std::lock_guard lock(mutex);
	const auto found = entries.find(steamId);
	if (found == entries.end())
	{
		return std::nullopt;
	}
	if (found->second.expiresAt <= std::chrono::steady_clock::now())
	{
		Remove(found);
		return std::nullopt;
	}
	leastRecentlyUsed.splice(leastRecentlyUsed.begin(), leastRecentlyUsed, found->second.lruPosition);
	return found->second.settings;
}

bool PlayerSettingsCache::Put(std::uint64_t steamId, PlayerSettings settings)
{
	if (steamId == 0 || settings.settingsJson.size() > maxBytes)
	{
		return false;
	}

	std::lock_guard lock(mutex);
	const auto now = std::chrono::steady_clock::now();
	PurgeExpired(now);
	auto existing = entries.find(steamId);
	if (existing != entries.end() && existing->second.settings.revision > settings.revision)
	{
		return false;
	}
	if (existing != entries.end())
	{
		Remove(existing);
	}
	while (entries.size() >= maxEntries || bytesInUse > maxBytes - settings.settingsJson.size())
	{
		EvictLeastRecentlyUsed();
	}

	const std::size_t entryBytes = settings.settingsJson.size();
	leastRecentlyUsed.push_front(steamId);
	try
	{
		entries.emplace(steamId, Entry{std::move(settings), now + ttl, entryBytes, leastRecentlyUsed.begin()});
	}
	catch (...)
	{
		leastRecentlyUsed.pop_front();
		throw;
	}
	bytesInUse += entryBytes;
	return true;
}

bool PlayerSettingsCache::Invalidate(std::uint64_t steamId)
{
	std::lock_guard lock(mutex);
	const auto found = entries.find(steamId);
	if (found == entries.end())
	{
		return false;
	}
	Remove(found);
	return true;
}

void PlayerSettingsCache::Clear()
{
	std::lock_guard lock(mutex);
	entries.clear();
	leastRecentlyUsed.clear();
	bytesInUse = 0;
}

std::size_t PlayerSettingsCache::Size() const
{
	std::lock_guard lock(mutex);
	return entries.size();
}

std::size_t PlayerSettingsCache::BytesInUse() const
{
	std::lock_guard lock(mutex);
	return bytesInUse;
}

void PlayerSettingsCache::Remove(std::unordered_map<std::uint64_t, Entry>::iterator entry)
{
	bytesInUse -= entry->second.bytes;
	leastRecentlyUsed.erase(entry->second.lruPosition);
	entries.erase(entry);
}

void PlayerSettingsCache::PurgeExpired(std::chrono::steady_clock::time_point now)
{
	for (auto iterator = entries.begin(); iterator != entries.end();)
	{
		if (iterator->second.expiresAt <= now)
		{
			bytesInUse -= iterator->second.bytes;
			leastRecentlyUsed.erase(iterator->second.lruPosition);
			iterator = entries.erase(iterator);
		}
		else
		{
			++iterator;
		}
	}
}

void PlayerSettingsCache::EvictLeastRecentlyUsed()
{
	const auto leastRecentlyUsedEntry = entries.find(leastRecentlyUsed.back());
	Remove(leastRecentlyUsedEntry);
}
} // namespace ow::adapters
