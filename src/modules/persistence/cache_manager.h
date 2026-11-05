#pragma once

#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ow::persistence
{
struct CacheEntry
{
	std::string value;
	std::uint64_t revision {};
	// Shared stores enforce expiration remotely and may use time_point::max() here.
	std::chrono::steady_clock::time_point expiresAt;
};

class ICacheStore
{
public:
	virtual ~ICacheStore() = default;
	// Cache stores are accessed from asynchronous warmup workers and must be thread-safe.
	virtual std::optional<CacheEntry> Get(std::string_view key, std::uint64_t minimumRevision = 0) = 0;
	virtual bool Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl) = 0;
	virtual bool Invalidate(std::string_view key) = 0;
};

class MemoryCache final : public ICacheStore
{
public:
	explicit MemoryCache(std::size_t maxEntries = 65536, std::size_t maxBytes = 64 * 1024 * 1024);

	std::optional<CacheEntry> Get(std::string_view key, std::uint64_t minimumRevision = 0) override;
	bool Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl) override;
	bool Invalidate(std::string_view key) override;
	void Clear();
	std::size_t Size() const;
	std::size_t BytesInUse() const;

private:
	struct Entry
	{
		CacheEntry value;
		std::size_t bytes {};
		std::list<std::string>::iterator lruPosition {};
	};

	void Remove(std::unordered_map<std::string, Entry>::iterator entry);
	void PurgeExpired(std::chrono::steady_clock::time_point now);
	void EvictLeastRecentlyUsed();

	mutable std::mutex mutex;
	std::unordered_map<std::string, Entry> entries;
	std::list<std::string> leastRecentlyUsed;
	const std::size_t maxEntries;
	const std::size_t maxBytes;
	std::size_t bytesInUse {};
	bool enabled {true};
};

struct CacheLoadValue
{
	std::string value;
	std::uint64_t revision {};
};

enum class WarmupStatus
{
	NotRequested,
	Loading,
	Ready,
	Failed
};

enum class WarmupRequestResult
{
	Queued,
	AlreadyQueued,
	AlreadyCached,
	Rejected
};

class AsyncCacheWarmer
{
public:
	using Loader = std::function<std::optional<CacheLoadValue>()>;

	explicit AsyncCacheWarmer(ICacheStore& cache, std::size_t workerCount = 2,
							  std::size_t maxQueuedJobs = 4096);
	~AsyncCacheWarmer();

	AsyncCacheWarmer(const AsyncCacheWarmer&) = delete;
	AsyncCacheWarmer& operator=(const AsyncCacheWarmer&) = delete;

	WarmupRequestResult Request(std::string key, std::uint64_t minimumRevision,
								std::chrono::seconds ttl, int priority, Loader loader);
	WarmupStatus GetStatus(std::string_view key, std::uint64_t minimumRevision = 0,
						   std::string* error = nullptr);
	// False when the warmer was constructed with unusable limits; every request is then rejected.
	bool IsEnabled() const noexcept;

private:
	struct Job
	{
		std::string key;
		std::uint64_t minimumRevision {};
		std::chrono::seconds ttl {};
		int priority {};
		Loader loader;
	};

	void WorkerLoop();

	ICacheStore& cache;
	const std::size_t maxQueuedJobs;
	std::mutex mutex;
	std::condition_variable workAvailable;
	std::deque<Job> jobs;
	std::unordered_map<std::string, std::uint64_t> pending;
	std::unordered_map<std::string, std::string> failures;
	std::vector<std::thread> workers;
	bool stopping {};
	bool enabled {true};
};
} // namespace ow::persistence
