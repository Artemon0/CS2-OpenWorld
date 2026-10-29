#include "player_warmup.h"

#include <utility>

namespace ow::persistence
{
bool PlayerWarmupPlan::AddRequired(std::string key, std::uint64_t minimumRevision, std::chrono::seconds ttl,
								  AsyncCacheWarmer::Loader loader)
{
	return Add(Item{std::move(key), minimumRevision, ttl, std::move(loader), true});
}

bool PlayerWarmupPlan::AddDeferred(std::string key, std::uint64_t minimumRevision, std::chrono::seconds ttl,
								   AsyncCacheWarmer::Loader loader)
{
	return Add(Item{std::move(key), minimumRevision, ttl, std::move(loader), false});
}

bool PlayerWarmupPlan::Start(AsyncCacheWarmer& warmer)
{
	if (started || items.empty())
	{
		return false;
	}
	bool hasRequired = false;
	for (const Item& item : items)
	{
		hasRequired = hasRequired || item.required;
	}
	if (!hasRequired)
	{
		return false;
	}

	this->warmer = &warmer;
	started = true;
	bool allQueued = true;
	for (const Item& item : items)
	{
		if (item.required)
		{
			const WarmupRequestResult result =
				warmer.Request(item.key, item.minimumRevision, item.ttl, 100, item.loader);
			if (result == WarmupRequestResult::Rejected)
			{
				rejected.insert(item.key);
				allQueued = false;
			}
		}
	}
	for (const Item& item : items)
	{
		if (!item.required)
		{
			const WarmupRequestResult result =
				warmer.Request(item.key, item.minimumRevision, item.ttl, 0, item.loader);
			if (result == WarmupRequestResult::Rejected)
			{
				rejected.insert(item.key);
				allQueued = false;
			}
		}
	}
	return allQueued;
}

bool PlayerWarmupPlan::CanSpawn() const
{
	if (!started || !warmer)
	{
		return false;
	}
	for (const Item& item : items)
	{
		if (item.required && GetItemStatus(item) != WarmupStatus::Ready)
		{
			return false;
		}
	}
	return true;
}

bool PlayerWarmupPlan::IsFullyWarm() const
{
	if (!started || !warmer)
	{
		return false;
	}
	for (const Item& item : items)
	{
		if (GetItemStatus(item) != WarmupStatus::Ready)
		{
			return false;
		}
	}
	return true;
}

std::size_t PlayerWarmupPlan::GetPendingCount() const
{
	if (!started || !warmer)
	{
		return items.size();
	}
	std::size_t count = 0;
	for (const Item& item : items)
	{
		const WarmupStatus status = GetItemStatus(item);
		if (!rejected.contains(item.key) &&
			(status == WarmupStatus::Loading || status == WarmupStatus::NotRequested))
		{
			++count;
		}
	}
	return count;
}

std::vector<std::string> PlayerWarmupPlan::GetFailures() const
{
	std::vector<std::string> failures;
	if (!started || !warmer)
	{
		return failures;
	}
	for (const Item& item : items)
	{
		if (GetItemStatus(item) == WarmupStatus::Failed)
		{
			failures.push_back(item.key);
		}
	}
	for (const std::string& key : rejected)
	{
		failures.push_back(key);
	}
	return failures;
}

bool PlayerWarmupPlan::Add(Item item)
{
	if (started || item.key.empty() || item.minimumRevision == 0 || item.ttl <= std::chrono::seconds::zero() ||
		!item.loader || !keys.insert(item.key).second)
	{
		return false;
	}
	items.push_back(std::move(item));
	return true;
}

WarmupStatus PlayerWarmupPlan::GetItemStatus(const Item& item, std::string* error) const
{
	return warmer->GetStatus(item.key, item.minimumRevision, error);
}
} // namespace ow::persistence
