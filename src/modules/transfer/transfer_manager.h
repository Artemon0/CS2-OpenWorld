#pragma once

#include <chrono>
#include <cstddef>
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

	// Used to reject an idempotency-key replay that changed any part of the snapshot.
	bool operator==(const PlayerSnapshot&) const = default;
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

class ITransferService
{
public:
	virtual ~ITransferService() = default;

	virtual bool Request(TransferRecord transfer, bool steamAuthenticated,
						 std::chrono::system_clock::time_point now) = 0;
	virtual bool Advance(const std::string& transferId, TransferState next,
					 std::chrono::system_clock::time_point now) = 0;
	virtual bool Fail(const std::string& transferId, std::chrono::system_clock::time_point now) = 0;
	virtual bool Cancel(const std::string& transferId, std::chrono::system_clock::time_point now) = 0;
	virtual std::size_t Expire(std::chrono::system_clock::time_point now) = 0;
	virtual std::size_t PruneTerminal(std::chrono::system_clock::time_point now,
								  std::chrono::seconds retention) = 0;
	virtual std::optional<TransferRecord> Find(const std::string& transferId) const = 0;
	virtual std::vector<TransferRecord> GetPendingForPlayer(std::uint64_t steamId) const = 0;
};

class TransferManager final : public ITransferService
{
public:
	bool Request(TransferRecord transfer, bool steamAuthenticated,
				 std::chrono::system_clock::time_point now) override;
	bool Advance(const std::string& transferId, TransferState next,
				 std::chrono::system_clock::time_point now) override;
	bool Fail(const std::string& transferId, std::chrono::system_clock::time_point now) override;
	bool Cancel(const std::string& transferId, std::chrono::system_clock::time_point now) override;
	std::size_t Expire(std::chrono::system_clock::time_point now) override;
	std::size_t PruneTerminal(std::chrono::system_clock::time_point now,
							  std::chrono::seconds retention) override;
	std::optional<TransferRecord> Find(const std::string& transferId) const override;
	std::vector<TransferRecord> GetPendingForPlayer(std::uint64_t steamId) const override;

private:
	static bool IsTerminal(TransferState state);
	static bool IsAllowedTransition(TransferState current, TransferState next);

	std::unordered_map<std::string, TransferRecord> transfers;
	std::unordered_map<std::string, std::string> idempotencyIndex;
};
} // namespace ow::transfer
