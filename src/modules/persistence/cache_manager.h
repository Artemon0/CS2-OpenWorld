#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ow::persistence
{
struct CacheEntry
{
	std::string value;
	std::uint64_t revision {};
	std::chrono::steady_clock::time_point expiresAt;
};

class ICacheStore
{
public:
	virtual ~ICacheStore() = default;
	virtual std::optional<CacheEntry> Get(std::string_view key, std::uint64_t minimumRevision = 0) = 0;
	virtual bool Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl) = 0;
	virtual bool Invalidate(std::string_view key) = 0;
};

class MemoryCache final : public ICacheStore
{
public:
	std::optional<CacheEntry> Get(std::string_view key, std::uint64_t minimumRevision = 0) override;
	bool Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl) override;
	bool Invalidate(std::string_view key) override;
	void Clear();

private:
	std::unordered_map<std::string, CacheEntry> entries;
};
} // namespace ow::persistence
