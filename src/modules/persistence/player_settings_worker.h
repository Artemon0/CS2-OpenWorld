#pragma once

#include "adapters/persistence/player_settings_cache.h"
#include "core/async_worker.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ow::persistence
{
struct PlayerSettingsLoadResult
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	std::optional<adapters::PlayerSettings> settings;
	std::string error;
};

// Loads player settings off the game thread. The queue is bounded by maxOutstanding; a rejected
// configuration leaves the worker stopped and IsRunning() returns false (the plugin builds without
// exceptions, so the constructor never throws).
class PlayerSettingsWorker
{
public:
	using Loader =
		std::function<std::optional<adapters::PlayerSettings>(std::uint64_t steamId, std::string& error)>;

	explicit PlayerSettingsWorker(Loader loader, std::size_t maxOutstanding = 128);
	~PlayerSettingsWorker() = default;

	PlayerSettingsWorker(const PlayerSettingsWorker&) = delete;
	PlayerSettingsWorker& operator=(const PlayerSettingsWorker&) = delete;

	bool IsRunning() const noexcept;
	bool Request(std::uint64_t requestId, std::uint64_t steamId);
	std::optional<PlayerSettingsLoadResult> TryTakeResult();
	void Stop() noexcept;

private:
	struct Job
	{
		std::uint64_t requestId {};
		std::uint64_t steamId {};
	};

	using Worker = core::AsyncWorker<Job, PlayerSettingsLoadResult>;

	static Worker MakeWorker(Loader loader, std::size_t maxOutstanding);

	Worker worker;
};
} // namespace ow::persistence
