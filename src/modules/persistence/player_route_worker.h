#pragma once

#include "adapters/persistence/postgres_adapter.h"
#include "core/async_worker.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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

// Resolves and assigns player routes off the game thread. Fail-closed on invalid configuration:
// the worker stays stopped and IsRunning() reports false.
class PlayerRouteWorker
{
public:
	using Handler = std::function<void(const PlayerRouteJob&, PlayerRouteResult&)>;

	explicit PlayerRouteWorker(Handler handler, std::size_t maxOutstanding = 256);
	~PlayerRouteWorker() = default;

	PlayerRouteWorker(const PlayerRouteWorker&) = delete;
	PlayerRouteWorker& operator=(const PlayerRouteWorker&) = delete;

	bool IsRunning() const noexcept;
	bool RequestResolve(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
						std::string_view currentServerId, std::string_view lobbyServerId);
	bool RequestAssign(std::uint64_t requestId, std::uint64_t steamId, std::string_view environment,
					   std::string_view destinationServerId);
	std::optional<PlayerRouteResult> TryTakeResult();
	void Stop() noexcept;

private:
	using Worker = core::AsyncWorker<PlayerRouteJob, PlayerRouteResult>;

	static Worker MakeWorker(Handler handler, std::size_t maxOutstanding);

	bool Request(PlayerRouteJob job);

	Worker worker;
};
} // namespace ow::persistence
