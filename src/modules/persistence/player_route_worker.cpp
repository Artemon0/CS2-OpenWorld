#include "player_route_worker.h"

#include <utility>

namespace ow::persistence
{
namespace
{
bool IsValidRouteText(std::string_view value, std::size_t maxBytes)
{
	return !value.empty() && value.size() <= maxBytes && value.find('\0') == std::string_view::npos;
}
} // namespace

PlayerRouteWorker::PlayerRouteWorker(Handler handler, std::size_t maxOutstanding)
	: worker(MakeWorker(std::move(handler), maxOutstanding))
{
}

// Returns a stopped worker instead of throwing when the handler is missing.
PlayerRouteWorker::Worker PlayerRouteWorker::MakeWorker(Handler handler, std::size_t maxOutstanding)
{
	if (!handler)
	{
		return Worker {Worker::Handler {}, maxOutstanding};
	}
	return Worker {
		[handler = std::move(handler)](const PlayerRouteJob& job, PlayerRouteResult& result)
		{
			result.operation = job.operation;
			handler(job, result);
			if (result.error.empty() && result.serverId.empty())
			{
				result.error = "Player route handler returned no server.";
			}
		},
		maxOutstanding};
}

bool PlayerRouteWorker::IsRunning() const noexcept
{
	return worker.IsRunning();
}

bool PlayerRouteWorker::RequestResolve(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
									   std::string_view currentServerId, std::string_view lobbyServerId)
{
	if (!IsValidRouteText(environment, 64) || !IsValidRouteText(currentServerId, 64) ||
		!IsValidRouteText(lobbyServerId, 64))
	{
		return false;
	}
	return Request({requestId, steamId, PlayerRouteOperation::Resolve, std::string(environment),
					std::string(currentServerId), std::string(lobbyServerId), {}});
}

bool PlayerRouteWorker::RequestAssign(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
									  std::string_view destinationServerId)
{
	if (!IsValidRouteText(environment, 64) || !IsValidRouteText(destinationServerId, 64))
	{
		return false;
	}
	return Request({requestId, steamId, PlayerRouteOperation::Assign, std::string(environment), {},
					{}, std::string(destinationServerId)});
}

bool PlayerRouteWorker::Request(PlayerRouteJob job)
{
	if (job.requestId == 0 || job.steamId == 0)
	{
		return false;
	}
	return worker.Submit(std::move(job));
}

std::optional<PlayerRouteResult> PlayerRouteWorker::TryTakeResult()
{
	return worker.TryTakeResult();
}

void PlayerRouteWorker::Stop() noexcept
{
	worker.Stop();
}
} // namespace ow::persistence
