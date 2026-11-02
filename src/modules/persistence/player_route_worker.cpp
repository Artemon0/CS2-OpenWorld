#include "player_route_worker.h"

#include <exception>
#include <stdexcept>
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
	: handler(std::move(handler)), maxOutstanding(maxOutstanding)
{
	if (!this->handler || maxOutstanding == 0)
	{
		throw std::invalid_argument("Player route worker requires a handler and positive queue capacity.");
	}
	worker = std::thread(&PlayerRouteWorker::WorkerLoop, this);
}

PlayerRouteWorker::~PlayerRouteWorker()
{
	Stop();
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

std::optional<PlayerRouteResult> PlayerRouteWorker::TryTakeResult()
{
	std::lock_guard lock(mutex);
	if (results.empty())
	{
		return std::nullopt;
	}
	PlayerRouteResult result = std::move(results.front());
	results.pop_front();
	--outstanding;
	return result;
}

void PlayerRouteWorker::Stop()
{
	{
		std::lock_guard lock(mutex);
		if (!stopping)
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

void PlayerRouteWorker::WorkerLoop()
{
	while (true)
	{
		PlayerRouteJob job;
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

		PlayerRouteResult result;
		result.requestId = job.requestId;
		result.steamId = job.steamId;
		result.operation = job.operation;
		try
		{
			handler(job, result);
			if (result.error.empty() && result.serverId.empty())
			{
				result.error = "Player route handler returned no server.";
			}
		}
		catch (const std::exception& exception)
		{
			result.error = exception.what();
		}
		catch (...)
		{
			result.error = "Player route handler failed with an unknown exception.";
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
