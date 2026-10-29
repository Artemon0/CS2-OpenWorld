#include "player_settings_worker.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace ow::persistence
{
PlayerSettingsWorker::PlayerSettingsWorker(Loader loader, std::size_t maxOutstanding)
	: loader(std::move(loader)), maxOutstanding(maxOutstanding)
{
	if (!this->loader || maxOutstanding == 0)
	{
		throw std::invalid_argument("Player settings worker requires a loader and positive queue capacity.");
	}
	worker = std::thread(&PlayerSettingsWorker::WorkerLoop, this);
}

PlayerSettingsWorker::~PlayerSettingsWorker()
{
	Stop();
}

bool PlayerSettingsWorker::Request(std::uint64_t requestId, std::uint64_t steamId)
{
	if (requestId == 0 || steamId == 0)
	{
		return false;
	}
	{
		std::lock_guard lock(mutex);
		if (stopping || outstanding >= maxOutstanding)
		{
			return false;
		}
		jobs.push_back({requestId, steamId});
		++outstanding;
	}
	workAvailable.notify_one();
	return true;
}

std::optional<PlayerSettingsLoadResult> PlayerSettingsWorker::TryTakeResult()
{
	std::lock_guard lock(mutex);
	if (results.empty())
	{
		return std::nullopt;
	}
	PlayerSettingsLoadResult result = std::move(results.front());
	results.pop_front();
	--outstanding;
	return result;
}

void PlayerSettingsWorker::Stop()
{
	{
		std::lock_guard lock(mutex);
		if (stopping)
		{
			if (!worker.joinable())
			{
				return;
			}
		}
		else
		{
			stopping = true;
			outstanding -= jobs.size();
			jobs.clear();
		}
	}
	workAvailable.notify_all();
	if (worker.joinable())
	{
		worker.join();
	}
	std::lock_guard lock(mutex);
	results.clear();
	outstanding = 0;
}

void PlayerSettingsWorker::WorkerLoop()
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
			job = jobs.front();
			jobs.pop_front();
		}

		PlayerSettingsLoadResult result;
		result.requestId = job.requestId;
		result.steamId = job.steamId;
		try
		{
			result.settings = loader(job.steamId, result.error);
			if (!result.settings && result.error.empty())
			{
				result.error = "Player settings loader returned no data.";
			}
		}
		catch (const std::exception& exception)
		{
			result.error = exception.what();
		}
		catch (...)
		{
			result.error = "Player settings loader failed with an unknown exception.";
		}

		{
			std::lock_guard lock(mutex);
			if (stopping)
			{
				--outstanding;
				continue;
			}
			results.push_back(std::move(result));
		}
	}
}
} // namespace ow::persistence
