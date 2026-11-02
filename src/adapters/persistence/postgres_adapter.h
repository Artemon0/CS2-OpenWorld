#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "player_settings_cache.h"

namespace ow::adapters
{
struct CharacterSummary
{
	std::string characterId;
	std::string displayName;
};

struct CharacterData
{
	std::string characterId;
	std::uint64_t steamId {};
	std::string displayName;
	std::string stateJson;
	std::uint64_t revision {};
	std::string inventoryJson;
	std::uint64_t inventoryRevision {};
	std::int64_t balance {};
	std::uint64_t balanceRevision {};
};

struct MoneyTransactionResult
{
	std::int64_t balance {};
	bool duplicate {};
};

struct InventoryData
{
	std::uint64_t revision {};
	std::string itemsJson;
};

class PostgreSQLAdapter
{
public:
	PostgreSQLAdapter();
	~PostgreSQLAdapter();

	PostgreSQLAdapter(const PostgreSQLAdapter&) = delete;
	PostgreSQLAdapter& operator=(const PostgreSQLAdapter&) = delete;

	bool Connect(std::string_view connectionInfo);
	bool Ping();
	bool IsConnected() const;
	std::string GetLastError() const;

	std::optional<std::string> ResolvePlayerServerRoute(std::string_view environment, std::uint64_t steamId,
														std::string_view currentServerId,
														std::string_view lobbyServerId);
	std::optional<std::string> AssignPlayerServerRoute(std::string_view environment, std::uint64_t steamId,
													   std::string_view destinationServerId);
	std::optional<std::string> CreateCharacter(std::uint64_t steamId, std::string_view displayName);
	std::optional<std::vector<CharacterSummary>> ListCharacters(std::uint64_t steamId);
	std::optional<CharacterData> LoadCharacter(std::string_view characterId);
	std::optional<CharacterData> LoadCharacterForPlayer(std::uint64_t steamId, std::string_view characterId);
	std::optional<InventoryData> LoadInventory(std::string_view characterId);
	std::optional<PlayerSettings> LoadPlayerSettings(std::uint64_t steamId);
	bool SaveCharacterState(std::string_view characterId, std::uint64_t expectedRevision,
							std::string_view stateJson, std::uint64_t& newRevision);
	bool SaveInventory(std::string_view characterId, std::uint64_t expectedRevision,
					   std::string_view itemsJson, std::uint64_t& newRevision);
	bool SavePlayerSettings(std::uint64_t steamId, std::uint64_t expectedRevision,
							std::string_view settingsJson, std::uint64_t& newRevision);
	std::optional<MoneyTransactionResult> ApplyMoneyTransaction(std::string_view characterId,
																 std::string_view idempotencyKey,
																 std::int64_t amount, std::string_view reason);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace ow::adapters
