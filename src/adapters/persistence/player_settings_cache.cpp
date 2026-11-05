#include "player_settings_cache.h"

#include <utility>

namespace ow::adapters
{
PlayerSettingsCache::PlayerSettingsCache(std::size_t maxEntries, std::size_t maxBytes, std::chrono::milliseconds ttl)
	: maxEntries(maxEntries), maxBytes(maxBytes), ttl(ttl)
{
	// Fail-closed: the plugin is built without exceptions, so an invalid configuration disables the
	// cache (every lookup misses) instead of throwing or aborting the server.
	if (maxEntries == 0 || maxBytes == 0 || ttl <= std::chrono::milliseconds::zero())
	{
		enabled = false;
	}
}

bool PlayerSettingsCache::IsEnabled() const noexcept
{
	return enabled;
}

std::optional<PlayerSettings> PlayerSettingsCache::Get(std::uint64_t steamId)
{
	if (!enabled || steamId == 0)
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
	if (!enabled || steamId == 0 || settings.settingsJson.size() > maxBytes)
	{
		return false;
	}

	std::lock_guard lock(mutex);
	const auto now = std::chrono::steady_clock::now();
	PurgeExpired(now);
	const auto existing = entries.find(steamId);
	if (existing != entries.end() && existing->second.settings.revision > settings.revision)
	{
		return false;
	}
	if (existing != entries.end())
	{
		Remove(existing);
	}

	const std::size_t entryBytes = settings.settingsJson.size();
	while (entries.size() >= maxEntries || (bytesInUse + entryBytes > maxBytes && !entries.empty()))
	{
		EvictLeastRecentlyUsed();
	}

	// Insert the map node first: the LRU list must never hold an iterator into a node that failed to
	// be created, which is what corrupted the heap in the previous implementation.
	const auto inserted = entries.try_emplace(steamId, Entry {std::move(settings), now + ttl, entryBytes, {}});
	leastRecentlyUsed.push_front(steamId);
	inserted.first->second.lruPosition = leastRecentlyUsed.begin();
	bytesInUse += entryBytes;
	return true;
}

bool PlayerSettingsCache::Invalidate(std::uint64_t steamId)
{
	if (!enabled)
	{
		return false;
	}
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
	if (leastRecentlyUsed.empty())
	{
		return;
	}
	const auto leastRecentlyUsedEntry = entries.find(leastRecentlyUsed.back());
	if (leastRecentlyUsedEntry == entries.end())
	{
		// Defensive: a desynchronised LRU list must never reach erase() with a bogus key.
		leastRecentlyUsed.pop_back();
		return;
	}
	Remove(leastRecentlyUsedEntry);
}
} // namespace ow::adapters
