#pragma once

#include "adapters/persistence/postgres_adapter.h"
#include "core/async_worker.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ow::persistence
{
enum class PlayerCharacterOperation
{
	List,
	Create,
	Load
};

struct PlayerCharacterJob
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerCharacterOperation operation {PlayerCharacterOperation::List};
	std::string value;
};

struct PlayerCharacterResult
{
	std::uint64_t requestId {};
	std::uint64_t steamId {};
	PlayerCharacterOperation operation {PlayerCharacterOperation::List};
	std::vector<adapters::CharacterSummary> characters;
	std::optional<adapters::CharacterData> character;
	std::string error;
};

// Runs character list/create/load requests off the game thread. Fail-closed on invalid
// configuration: the worker stays stopped and IsRunning() reports false.
class PlayerCharacterWorker
{
public:
	using Loader = std::function<void(const PlayerCharacterJob&, PlayerCharacterResult&)>;

	explicit PlayerCharacterWorker(Loader loader, std::size_t maxOutstanding = 128);
	~PlayerCharacterWorker() = default;

	PlayerCharacterWorker(const PlayerCharacterWorker&) = delete;
	PlayerCharacterWorker& operator=(const PlayerCharacterWorker&) = delete;

	bool IsRunning() const noexcept;
	bool RequestList(std::uint64_t requestId, std::uint64_t steamId);
	bool RequestCreate(std::uint64_t requestId, std::uint64_t steamId, std::string_view displayName);
	bool RequestLoad(std::uint64_t requestId, std::uint64_t steamId, std::string_view characterId);
	std::optional<PlayerCharacterResult> TryTakeResult();
	void Stop() noexcept;

private:
	using Worker = core::AsyncWorker<PlayerCharacterJob, PlayerCharacterResult>;

	static Worker MakeWorker(Loader loader, std::size_t maxOutstanding);

	bool Request(PlayerCharacterJob job);

	Worker worker;
};
} // namespace ow::persistence
