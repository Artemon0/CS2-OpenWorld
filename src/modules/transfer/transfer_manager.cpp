#include "transfer_manager.h"

#include <cmath>
#include <utility>

namespace ow::transfer
{
bool TransferManager::Request(TransferRecord transfer, bool steamAuthenticated, std::chrono::system_clock::time_point now)
{
	if (!steamAuthenticated || transfer.transferId.empty() || transfer.idempotencyKey.empty() || transfer.transferToken.empty() ||
		transfer.snapshot.steamId == 0 ||
		transfer.snapshot.characterId.empty() || transfer.snapshot.sourceServerId.empty() ||
		transfer.snapshot.destinationServerId.empty() ||
		transfer.snapshot.sourceServerId == transfer.snapshot.destinationServerId ||
		transfer.snapshot.dataRevision == 0 || transfer.expiresAt <= now ||
		!std::isfinite(transfer.snapshot.positionX) || !std::isfinite(transfer.snapshot.positionY) ||
		!std::isfinite(transfer.snapshot.positionZ))
	{
		return false;
	}

	if (const auto key = idempotencyIndex.find(transfer.idempotencyKey); key != idempotencyIndex.end())
	{
		const auto existing = transfers.find(key->second);
		// A retry must reproduce the original request exactly; anything else reuses the key.
		return key->second == transfer.transferId && existing != transfers.end() &&
			   existing->second.snapshot == transfer.snapshot &&
			   existing->second.transferToken == transfer.transferToken &&
			   existing->second.expiresAt == transfer.expiresAt;
	}
	if (transfers.find(transfer.transferId) != transfers.end())
	{
		return false;
	}
	for (const auto& entry : transfers)
	{
		const TransferRecord& existing = entry.second;
		// An abandoned (expired) record must not block a fresh transfer until Expire() runs.
		if ((existing.snapshot.steamId == transfer.snapshot.steamId ||
			 existing.snapshot.characterId == transfer.snapshot.characterId) &&
			!IsTerminal(existing.state) && now < existing.expiresAt)
		{
			return false;
		}
	}

	transfer.state = TransferState::Requested;
	transfer.createdAt = now;
	transfer.updatedAt = now;
	idempotencyIndex.emplace(transfer.idempotencyKey, transfer.transferId);
	transfers.emplace(transfer.transferId, std::move(transfer));
	return true;
}

bool TransferManager::Advance(const std::string& transferId, TransferState next, std::chrono::system_clock::time_point now)
{
	const auto found = transfers.find(transferId);
	if (found == transfers.end())
	{
		return false;
	}
	TransferRecord& transfer = found->second;
	if (now >= transfer.expiresAt && !IsTerminal(transfer.state))
	{
		transfer.state = TransferState::Expired;
		transfer.updatedAt = now;
		return false;
	}
	if (transfer.state == next)
	{
		return true;
	}
	if (IsTerminal(transfer.state))
	{
		return false;
	}
	if (!IsAllowedTransition(transfer.state, next))
	{
		return false;
	}

	transfer.state = next;
	transfer.updatedAt = now;
	return true;
}

bool TransferManager::Fail(const std::string& transferId, std::chrono::system_clock::time_point now)
{
	const auto found = transfers.find(transferId);
	if (found == transfers.end() || IsTerminal(found->second.state))
	{
		return false;
	}
	if (now >= found->second.expiresAt)
	{
		found->second.state = TransferState::Expired;
		found->second.updatedAt = now;
		return false;
	}
	found->second.state = TransferState::Failed;
	found->second.updatedAt = now;
	return true;
}

bool TransferManager::Cancel(const std::string& transferId, std::chrono::system_clock::time_point now)
{
	const auto found = transfers.find(transferId);
	if (found == transfers.end() || IsTerminal(found->second.state))
	{
		return false;
	}
	if (now >= found->second.expiresAt)
	{
		found->second.state = TransferState::Expired;
		found->second.updatedAt = now;
		return false;
	}
	if (found->second.state == TransferState::Redirecting || found->second.state == TransferState::Arriving)
	{
		return false;
	}
	found->second.state = TransferState::Cancelled;
	found->second.updatedAt = now;
	return true;
}

std::size_t TransferManager::Expire(std::chrono::system_clock::time_point now)
{
	std::size_t expiredCount = 0;
	for (auto& entry : transfers)
	{
		if (!IsTerminal(entry.second.state) && now >= entry.second.expiresAt)
		{
			entry.second.state = TransferState::Expired;
			entry.second.updatedAt = now;
			++expiredCount;
		}
	}
	return expiredCount;
}

std::size_t TransferManager::PruneTerminal(std::chrono::system_clock::time_point now, std::chrono::seconds retention)
{
	if (retention <= std::chrono::seconds::zero())
	{
		return 0;
	}

	std::size_t prunedCount = 0;
	for (auto iterator = transfers.begin(); iterator != transfers.end();)
	{
		const TransferRecord& transfer = iterator->second;
		if (IsTerminal(transfer.state) && now - transfer.updatedAt >= retention)
		{
			const auto key = idempotencyIndex.find(transfer.idempotencyKey);
			if (key != idempotencyIndex.end() && key->second == transfer.transferId)
			{
				idempotencyIndex.erase(key);
			}
			iterator = transfers.erase(iterator);
			++prunedCount;
		}
		else
		{
			++iterator;
		}
	}
	return prunedCount;
}

std::optional<TransferRecord> TransferManager::Find(const std::string& transferId) const
{
	const auto found = transfers.find(transferId);
	return found == transfers.end() ? std::nullopt : std::optional<TransferRecord>(found->second);
}

std::vector<TransferRecord> TransferManager::GetPendingForPlayer(std::uint64_t steamId) const
{
	std::vector<TransferRecord> result;
	for (const auto& entry : transfers)
	{
		if (entry.second.snapshot.steamId == steamId && !IsTerminal(entry.second.state))
		{
			result.push_back(entry.second);
		}
	}
	return result;
}

bool TransferManager::IsTerminal(TransferState state)
{
	return state == TransferState::Completed || state == TransferState::Failed || state == TransferState::Expired ||
		   state == TransferState::Cancelled;
}

bool TransferManager::IsAllowedTransition(TransferState current, TransferState next)
{
	switch (current)
	{
	case TransferState::Requested:
		return next == TransferState::Validating || next == TransferState::Cancelled;
	case TransferState::Validating:
		return next == TransferState::DestinationReserved || next == TransferState::Failed || next == TransferState::Cancelled;
	case TransferState::DestinationReserved:
		return next == TransferState::StateSaved || next == TransferState::Failed || next == TransferState::Cancelled;
	case TransferState::StateSaved:
		return next == TransferState::Redirecting || next == TransferState::Failed || next == TransferState::Cancelled;
	case TransferState::Redirecting:
		return next == TransferState::Arriving || next == TransferState::Failed;
	case TransferState::Arriving:
		return next == TransferState::Completed || next == TransferState::Failed;
	case TransferState::Completed:
	case TransferState::Failed:
	case TransferState::Expired:
	case TransferState::Cancelled:
		return false;
	}
	return false;
}

} // namespace ow::transfer
