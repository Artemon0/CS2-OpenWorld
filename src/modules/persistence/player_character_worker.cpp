#include "player_character_worker.h"

#include <utility>

namespace ow::persistence
{
PlayerCharacterWorker::PlayerCharacterWorker(Loader loader, std::size_t maxOutstanding)
	: worker(MakeWorker(std::move(loader), maxOutstanding))
{
}

// Returns a stopped worker instead of throwing when the loader is missing.
PlayerCharacterWorker::Worker PlayerCharacterWorker::MakeWorker(Loader loader, std::size_t maxOutstanding)
{
	if (!loader)
	{
		return Worker {Worker::Handler {}, maxOutstanding};
	}
	return Worker {
		[loader = std::move(loader)](const PlayerCharacterJob& job, PlayerCharacterResult& result)
		{
			result.operation = job.operation;
			loader(job, result);
			if (result.error.empty() && job.operation != PlayerCharacterOperation::List && !result.character)
			{
				result.error = "Character loader returned no character data.";
			}
		},
		maxOutstanding};
}

bool PlayerCharacterWorker::IsRunning() const noexcept
{
	return worker.IsRunning();
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
	return worker.Submit(std::move(job));
}

std::optional<PlayerCharacterResult> PlayerCharacterWorker::TryTakeResult()
{
	return worker.TryTakeResult();
}

void PlayerCharacterWorker::Stop() noexcept
{
	worker.Stop();
}
} // namespace ow::persistence
