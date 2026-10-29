#include "cache_manager.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace ow::persistence
{
MemoryCache::MemoryCache(std::size_t maxEntries, std::size_t maxBytes)
	: maxEntries(maxEntries), maxBytes(maxBytes)
{
	if (maxEntries == 0 || maxBytes == 0)
	{
		throw std::invalid_argument("Memory cache capacity must be positive.");
	}
}

std::optional<CacheEntry> MemoryCache::Get(std::string_view key, std::uint64_t minimumRevision)
{
	std::lock_guard lock(mutex);
	const auto found = entries.find(std::string(key));
	if (found == entries.end())
	{
		return std::nullopt;
	}

	const auto now = std::chrono::steady_clock::now();
	if (found->second.value.expiresAt <= now)
	{
		Remove(found);
		return std::nullopt;
	}
	if (found->second.value.revision < minimumRevision)
	{
		return std::nullopt;
	}
	leastRecentlyUsed.splice(leastRecentlyUsed.begin(), leastRecentlyUsed, found->second.lruPosition);
	return found->second.value;
}

bool MemoryCache::Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl)
{
	if (key.empty() || revision == 0 || ttl <= std::chrono::seconds::zero())
	{
		return false;
	}

	std::lock_guard lock(mutex);
	const auto now = std::chrono::steady_clock::now();
	PurgeExpired(now);
	auto existing = entries.find(key);
	if (existing != entries.end() && existing->second.value.revision > revision)
	{
		return false;
	}
	if (key.size() > maxBytes || value.size() > maxBytes - key.size())
	{
		return false;
	}

	const std::size_t entryBytes = key.size() + value.size();
	if (existing != entries.end())
	{
		Remove(existing);
	}
	while (entries.size() >= maxEntries || bytesInUse > maxBytes - entryBytes)
	{
		EvictLeastRecentlyUsed();
	}
	leastRecentlyUsed.push_front(key);
	try
	{
		entries.emplace(std::move(key),
						Entry{CacheEntry{std::move(value), revision, now + ttl}, entryBytes,
							  leastRecentlyUsed.begin()});
	}
	catch (...)
	{
		leastRecentlyUsed.pop_front();
		throw;
	}
	bytesInUse += entryBytes;
	return true;
}

bool MemoryCache::Invalidate(std::string_view key)
{
	std::lock_guard lock(mutex);
	const auto found = entries.find(std::string(key));
	if (found == entries.end())
	{
		return false;
	}
	Remove(found);
	return true;
}

void MemoryCache::Clear()
{
	std::lock_guard lock(mutex);
	entries.clear();
	leastRecentlyUsed.clear();
	bytesInUse = 0;
}

std::size_t MemoryCache::Size() const
{
	std::lock_guard lock(mutex);
	return entries.size();
}

std::size_t MemoryCache::BytesInUse() const
{
	std::lock_guard lock(mutex);
	return bytesInUse;
}

void MemoryCache::Remove(std::unordered_map<std::string, Entry>::iterator entry)
{
	bytesInUse -= entry->second.bytes;
	leastRecentlyUsed.erase(entry->second.lruPosition);
	entries.erase(entry);
}

void MemoryCache::PurgeExpired(std::chrono::steady_clock::time_point now)
{
	for (auto iterator = entries.begin(); iterator != entries.end();)
	{
		if (iterator->second.value.expiresAt <= now)
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

void MemoryCache::EvictLeastRecentlyUsed()
{
	const auto leastRecentlyUsedEntry = entries.find(leastRecentlyUsed.back());
	Remove(leastRecentlyUsedEntry);
}

AsyncCacheWarmer::AsyncCacheWarmer(ICacheStore& cache, std::size_t workerCount, std::size_t maxQueuedJobs)
	: cache(cache), maxQueuedJobs(maxQueuedJobs)
{
	if (workerCount == 0 || maxQueuedJobs == 0)
	{
		throw std::invalid_argument("Cache warmer requires at least one worker and one queue slot.");
	}

	workers.reserve(workerCount);
	try
	{
		for (std::size_t index = 0; index < workerCount; ++index)
		{
			workers.emplace_back(&AsyncCacheWarmer::WorkerLoop, this);
		}
	}
	catch (...)
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		workAvailable.notify_all();
		for (std::thread& worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
		throw;
	}
}

AsyncCacheWarmer::~AsyncCacheWarmer()
{
	{
		std::lock_guard lock(mutex);
		stopping = true;
		jobs.clear();
		pending.clear();
	}
	workAvailable.notify_all();
	for (std::thread& worker : workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
}

WarmupRequestResult AsyncCacheWarmer::Request(std::string key, std::uint64_t minimumRevision,
											  std::chrono::seconds ttl, int priority, Loader loader)
{
	if (key.empty() || minimumRevision == 0 || ttl <= std::chrono::seconds::zero() || !loader)
	{
		return WarmupRequestResult::Rejected;
	}
	if (cache.Get(key, minimumRevision))
	{
		return WarmupRequestResult::AlreadyCached;
	}

	{
		std::lock_guard lock(mutex);
		if (stopping)
		{
			return WarmupRequestResult::Rejected;
		}
		if (const auto found = pending.find(key); found != pending.end())
		{
			return found->second == minimumRevision ? WarmupRequestResult::AlreadyQueued
													: WarmupRequestResult::Rejected;
		}
		if (jobs.size() >= maxQueuedJobs)
		{
			return WarmupRequestResult::Rejected;
		}
		failures.erase(key);
		pending.emplace(key, minimumRevision);
		jobs.push_back(Job{std::move(key), minimumRevision, ttl, priority, std::move(loader)});
	}
	workAvailable.notify_one();
	return WarmupRequestResult::Queued;
}

WarmupStatus AsyncCacheWarmer::GetStatus(std::string_view key, std::uint64_t minimumRevision,
										 std::string* error)
{
	if (cache.Get(key, minimumRevision))
	{
		if (error)
		{
			error->clear();
		}
		return WarmupStatus::Ready;
	}

	std::lock_guard lock(mutex);
	if (const auto failure = failures.find(std::string(key)); failure != failures.end())
	{
		if (error)
		{
			*error = failure->second;
		}
		return WarmupStatus::Failed;
	}
	if (const auto active = pending.find(std::string(key)); active != pending.end())
	{
		if (error)
		{
			error->clear();
		}
		return active->second >= minimumRevision ? WarmupStatus::Loading : WarmupStatus::NotRequested;
	}
	if (error)
	{
		error->clear();
	}
	return WarmupStatus::NotRequested;
}

void AsyncCacheWarmer::WorkerLoop()
{
	while (true)
	{
		Job job;
		{
			std::unique_lock lock(mutex);
			workAvailable.wait(lock, [this] { return stopping || !jobs.empty(); });
			if (stopping)
			{
				return;
			}
			const auto next = std::max_element(jobs.begin(), jobs.end(),
											   [](const Job& left, const Job& right) {
												   return left.priority < right.priority;
											   });
			job = std::move(*next);
			jobs.erase(next);
		}

		std::string failure;
		try
		{
			const std::optional<CacheLoadValue> loaded = job.loader();
			if (!loaded)
			{
				failure = "Cache loader returned no value.";
			}
			else if (loaded->revision < job.minimumRevision)
			{
				failure = "Cache loader returned a stale revision.";
			}
			else if (!cache.Put(job.key, loaded->value, loaded->revision, job.ttl) &&
					 !cache.Get(job.key, job.minimumRevision))
			{
				failure = "Cache store rejected the loaded value.";
			}
		}
		catch (const std::exception& exception)
		{
			failure = exception.what();
		}
		catch (...)
		{
			failure = "Cache loader failed with an unknown exception.";
		}

		{
			std::lock_guard lock(mutex);
			pending.erase(job.key);
			if (!failure.empty())
			{
				failures.insert_or_assign(job.key, std::move(failure));
			}
		}
	}
}
} // namespace ow::persistence
