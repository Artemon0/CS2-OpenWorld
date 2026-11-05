#include "player_settings_worker.h"

#include <utility>

namespace ow::persistence
{
PlayerSettingsWorker::PlayerSettingsWorker(Loader loader, std::size_t maxOutstanding)
	: worker(MakeWorker(std::move(loader), maxOutstanding))
{
}

// Returns a stopped worker instead of throwing when the loader is missing.
PlayerSettingsWorker::Worker PlayerSettingsWorker::MakeWorker(Loader loader, std::size_t maxOutstanding)
{
	if (!loader)
	{
		return Worker {Worker::Handler {}, maxOutstanding};
	}
	return Worker {
		[loader = std::move(loader)](const Job& job, PlayerSettingsLoadResult& result)
		{
			result.settings = loader(job.steamId, result.error);
			if (!result.settings && result.error.empty())
			{
				result.error = "Player settings loader returned no data.";
			}
		},
		maxOutstanding};
}

bool PlayerSettingsWorker::IsRunning() const noexcept
{
	return worker.IsRunning();
}

bool PlayerSettingsWorker::Request(std::uint64_t requestId, std::uint64_t steamId)
{
	if (requestId == 0 || steamId == 0)
	{
		return false;
	}
	return worker.Submit({requestId, steamId});
}

std::optional<PlayerSettingsLoadResult> PlayerSettingsWorker::TryTakeResult()
{
	return worker.TryTakeResult();
}

void PlayerSettingsWorker::Stop() noexcept
{
	worker.Stop();
}
} // namespace ow::persistence
