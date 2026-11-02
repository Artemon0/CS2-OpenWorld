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
#include <thread>

namespace ow::persistence
{
enum class PlayerRouteOperation
{
	Resolve,
	Assign
};

struct PlayerRouteJob
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerRouteOperation operation {PlayerRouteOperation::Resolve};
	std::string environment;
	std::string currentServerId;
	std::string lobbyServerId;
	std::string destinationServerId;
};

struct PlayerRouteResult
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerRouteOperation operation {PlayerRouteOperation::Resolve};
	std::string serverId;
	std::string error;
};

class PlayerRouteWorker
{
public:
	using Handler = std::function<void(const PlayerRouteJob&, PlayerRouteResult&)>;

	explicit PlayerRouteWorker(Handler handler, std::size_t maxOutstanding = 256);
	~PlayerRouteWorker();

	PlayerRouteWorker(const PlayerRouteWorker&) = delete;
	PlayerRouteWorker& operator=(const PlayerRouteWorker&) = delete;

	bool RequestResolve(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
						std::string_view currentServerId, std::string_view lobbyServerId);
	bool RequestAssign(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
					   std::string_view destinationServerId);
	std::optional<PlayerRouteResult> TryTakeResult();
	void Stop();

private:
	bool Request(PlayerRouteJob job);
	void WorkerLoop();

	Handler handler;
	const std::size_t maxOutstanding;
	std::mutex mutex;
	std::condition_variable workAvailable;
	std::deque<PlayerRouteJob> jobs;
	std::deque<PlayerRouteResult> results;
	std::thread worker;
	std::size_t outstanding {};
	bool stopping {};
};
} // namespace ow::persistence
