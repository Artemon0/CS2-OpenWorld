#pragma once

#include "cache_manager.h"

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

namespace ow::persistence
{
class PlayerWarmupPlan
{
public:
	bool AddRequired(std::string key, std::uint64_t minimumRevision, std::chrono::seconds ttl,
					 AsyncCacheWarmer::Loader loader);
	bool AddDeferred(std::string key, std::uint64_t minimumRevision, std::chrono::seconds ttl,
					 AsyncCacheWarmer::Loader loader);
	bool Start(AsyncCacheWarmer& warmer);

	bool CanSpawn() const;
	bool CanPlay() const { return CanSpawn(); }
	bool IsFullyWarm() const;
	std::size_t GetPendingCount() const;
	std::vector<std::string> GetFailures() const;

private:
	struct Item
	{
		std::string key;
		std::uint64_t minimumRevision {};
		std::chrono::seconds ttl {};
		AsyncCacheWarmer::Loader loader;
		bool required {};
	};

	bool Add(Item item);
	WarmupStatus GetItemStatus(const Item& item, std::string* error = nullptr) const;

	std::vector<Item> items;
	std::unordered_set<std::string> keys;
	std::unordered_set<std::string> rejected;
	AsyncCacheWarmer* warmer {};
	bool started {};
};
} // namespace ow::persistence
