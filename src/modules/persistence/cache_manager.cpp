#include "cache_manager.h"

#include <utility>

namespace ow::persistence
{
std::optional<CacheEntry> MemoryCache::Get(std::string_view key, std::uint64_t minimumRevision)
{
	const auto found = entries.find(std::string(key));
	if (found == entries.end())
	{
		return std::nullopt;
	}

	if (found->second.expiresAt <= std::chrono::steady_clock::now())
	{
		entries.erase(found);
		return std::nullopt;
	}
	if (found->second.revision < minimumRevision)
	{
		return std::nullopt;
	}
	return found->second;
}

bool MemoryCache::Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl)
{
	if (key.empty() || revision == 0 || ttl <= std::chrono::seconds::zero())
	{
		return false;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto existing = entries.find(key);
	if (existing != entries.end() && existing->second.expiresAt <= now)
	{
		entries.erase(existing);
	}
	else if (existing != entries.end() && existing->second.revision > revision)
	{
		return false;
	}

	entries.insert_or_assign(std::move(key),
							 CacheEntry{std::move(value), revision, now + ttl});
	return true;
}

bool MemoryCache::Invalidate(std::string_view key)
{
	return entries.erase(std::string(key)) != 0;
}

void MemoryCache::Clear()
{
	entries.clear();
}
} // namespace ow::persistence
