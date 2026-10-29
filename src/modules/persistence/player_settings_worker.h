#pragma once

#include "adapters/persistence/player_settings_cache.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ow::persistence
{
struct PlayerSettingsLoadResult
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	std::optional<adapters::PlayerSettings> settings;
	std::string error;
};

class PlayerSettingsWorker
{
public:
	using Loader =
		std::function<std::optional<adapters::PlayerSettings>(std::uint64_t steamId, std::string& error)>;

	explicit PlayerSettingsWorker(Loader loader, std::size_t maxOutstanding = 128);
	~PlayerSettingsWorker();

	PlayerSettingsWorker(const PlayerSettingsWorker&) = delete;
	PlayerSettingsWorker& operator=(const PlayerSettingsWorker&) = delete;

	bool Request(std::uint64_t requestId, std::uint64_t steamId);
	std::optional<PlayerSettingsLoadResult> TryTakeResult();
	void Stop();

private:
	struct Job
	{
		std::uint64_t requestId {};
		std::uint64_t steamId {};
	};

	void WorkerLoop();

	Loader loader;
	const std::size_t maxOutstanding;
	std::mutex mutex;
	std::condition_variable workAvailable;
	std::deque<Job> jobs;
	std::deque<PlayerSettingsLoadResult> results;
	std::thread worker;
	std::size_t outstanding {};
	bool stopping {};
};
} // namespace ow::persistence
