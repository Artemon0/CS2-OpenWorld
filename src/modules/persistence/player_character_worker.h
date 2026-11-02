#pragma once

#include "adapters/persistence/postgres_adapter.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ow::persistence
{
enum class PlayerCharacterOperation
{
	List,
	Create,
	Load
};

struct PlayerCharacterJob
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerCharacterOperation operation {PlayerCharacterOperation::List};
	std::string value;
};

struct PlayerCharacterResult
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerCharacterOperation operation {PlayerCharacterOperation::List};
	std::vector<adapters::CharacterSummary> characters;
	std::optional<adapters::CharacterData> character;
	std::string error;
};

class PlayerCharacterWorker
{
public:
	using Loader = std::function<void(const PlayerCharacterJob&, PlayerCharacterResult&)>;

	explicit PlayerCharacterWorker(Loader loader, std::size_t maxOutstanding = 128);
	~PlayerCharacterWorker();

	PlayerCharacterWorker(const PlayerCharacterWorker&) = delete;
	PlayerCharacterWorker& operator=(const PlayerCharacterWorker&) = delete;

	bool RequestList(std::uint64_t requestId, std::uint64_t steamId);
	bool RequestCreate(std::uint64_t requestId, std::uint64_t steamId, std::string_view displayName);
	bool RequestLoad(std::uint64_t requestId, std::uint64_t steamId, std::string_view characterId);
	std::optional<PlayerCharacterResult> TryTakeResult();
	void Stop();

private:
	bool Request(PlayerCharacterJob job);
	void WorkerLoop();

	Loader loader;
	const std::size_t maxOutstanding;
	std::mutex mutex;
	std::condition_variable workAvailable;
	std::deque<PlayerCharacterJob> jobs;
	std::deque<PlayerCharacterResult> results;
	std::thread worker;
	std::size_t outstanding {};
	bool stopping {};
};
} // namespace ow::persistence
