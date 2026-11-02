#include "player_character_worker.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace ow::persistence
{
PlayerCharacterWorker::PlayerCharacterWorker(Loader loader, std::size_t maxOutstanding)
	: loader(std::move(loader)), maxOutstanding(maxOutstanding)
{
	if (!this->loader || maxOutstanding == 0)
	{
		throw std::invalid_argument("Player character worker requires a loader and positive queue capacity.");
	}
	worker = std::thread(&PlayerCharacterWorker::WorkerLoop, this);
}

PlayerCharacterWorker::~PlayerCharacterWorker()
{
	Stop();
}

bool PlayerCharacterWorker::RequestList(std::uint64_t requestId, std::uint64_t steamId)
{
	return Request({requestId, steamId, PlayerCharacterOperation::List, {}});
}

bool PlayerCharacterWorker::RequestCreate(std::uint64_t requestId, std::uint64_t steamId,
										  std::string_view displayName)
{
	if (displayName.empty() || displayName.size() > 64 || displayName.find('\0') != std::string_view::npos)
	{
		return false;
	}
	return Request({requestId, steamId, PlayerCharacterOperation::Create, std::string(displayName)});
}

bool PlayerCharacterWorker::RequestLoad(std::uint64_t requestId, std::uint64_t steamId,
										std::string_view characterId)
{
	if (characterId.empty() || characterId.size() > 64 || characterId.find('\0') != std::string_view::npos)
	{
		return false;
	}
	return Request({requestId, steamId, PlayerCharacterOperation::Load, std::string(characterId)});
}

bool PlayerCharacterWorker::Request(PlayerCharacterJob job)
{
	if (job.requestId == 0 || job.steamId == 0)
	{
		return false;
	}
	{
		std::lock_guard lock(mutex);
		if (stopping || outstanding >= maxOutstanding)
		{
			return false;
		}
		jobs.push_back(std::move(job));
		++outstanding;
	}
	workAvailable.notify_one();
	return true;
}

std::optional<PlayerCharacterResult> PlayerCharacterWorker::TryTakeResult()
{
	std::lock_guard lock(mutex);
	if (results.empty())
	{
		return std::nullopt;
	}
	PlayerCharacterResult result = std::move(results.front());
	results.pop_front();
	--outstanding;
	return result;
}

void PlayerCharacterWorker::Stop()
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

void PlayerCharacterWorker::WorkerLoop()
{
	while (true)
	{
		PlayerCharacterJob job;
		{
			std::unique_lock lock(mutex);
			workAvailable.wait(lock, [this] { return stopping || !jobs.empty(); });
			if (stopping)
			{
				return;
			}
			job = std::move(jobs.front());
			jobs.pop_front();
		}

		PlayerCharacterResult result;
		result.requestId = job.requestId;
		result.steamId = job.steamId;
		result.operation = job.operation;
		try
		{
			loader(job, result);
			if (result.error.empty() && job.operation != PlayerCharacterOperation::List && !result.character)
			{
				result.error = "Character loader returned no character data.";
			}
		}
		catch (const std::exception& exception)
		{
			result.error = exception.what();
		}
		catch (...)
		{
			result.error = "Player character loader failed with an unknown exception.";
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
