#pragma once

#include "cache_manager.h"

#include <chrono>
#include <mutex>
#include <string>

namespace ow::persistence
{
struct RedisCacheConfig
{
	std::string host {"127.0.0.1"};
	std::uint16_t port {6379};
	int database {};
	std::string username;
	std::string password;
	std::string keyPrefix {"openworld:development:"};
	std::chrono::milliseconds timeout {1000};
	std::size_t maxValueBytes {4 * 1024 * 1024};
};

class RedisCache final : public ICacheStore
{
public:
	explicit RedisCache(RedisCacheConfig config);

	std::optional<CacheEntry> Get(std::string_view key, std::uint64_t minimumRevision = 0) override;
	bool Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl) override;
	bool Invalidate(std::string_view key) override;
	bool Ping();
	std::string GetLastError() const;

private:
	std::optional<std::string> Execute(const std::vector<std::string>& command, bool* isNil = nullptr);
	std::string MakeKey(std::string_view key) const;
	void SetLastError(std::string message) const;

	RedisCacheConfig config;
	mutable std::mutex errorMutex;
	mutable std::string lastError;
};
} // namespace ow::persistence
