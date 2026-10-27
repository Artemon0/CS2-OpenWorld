#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ow::transfer
{
enum class TransferState
{
	Requested,
	Validating,
	DestinationReserved,
	StateSaved,
	Redirecting,
	Arriving,
	Completed,
	Failed,
	Expired,
	Cancelled
};

struct PlayerSnapshot
{
	std::uint64_t steamId {};
	std::string characterId;
	std::string sourceServerId;
	std::string destinationServerId;
	std::string sourceSectorId;
	std::string destinationSectorId;
	double positionX {};
	double positionY {};
	double positionZ {};
	std::uint64_t dataRevision {};
};

struct TransferRecord
{
	std::string transferId;
	std::string idempotencyKey;
	std::string transferToken;
	PlayerSnapshot snapshot;
	TransferState state {TransferState::Requested};
	std::chrono::system_clock::time_point createdAt;
	std::chrono::system_clock::time_point updatedAt;
	std::chrono::system_clock::time_point expiresAt;
};

class TransferManager
{
public:
	bool Request(TransferRecord transfer, bool steamAuthenticated, std::chrono::system_clock::time_point now);
	bool Advance(const std::string& transferId, TransferState next, std::chrono::system_clock::time_point now);
	bool Fail(const std::string& transferId, std::chrono::system_clock::time_point now);
	bool Cancel(const std::string& transferId, std::chrono::system_clock::time_point now);
	std::size_t Expire(std::chrono::system_clock::time_point now);
	std::size_t PruneTerminal(std::chrono::system_clock::time_point now, std::chrono::seconds retention);
	std::optional<TransferRecord> Find(const std::string& transferId) const;
	std::vector<TransferRecord> GetPendingForPlayer(std::uint64_t steamId) const;

private:
	static bool IsTerminal(TransferState state);
	static bool IsAllowedTransition(TransferState current, TransferState next);

	std::unordered_map<std::string, TransferRecord> transfers;
	std::unordered_map<std::string, std::string> idempotencyIndex;
};
} // namespace ow::transfer
