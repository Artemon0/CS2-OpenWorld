#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "player_settings_cache.h"

namespace ow::adapters
{
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

	std::optional<std::string> CreateCharacter(std::uint64_t steamId, std::string_view displayName);
	std::optional<CharacterData> LoadCharacter(std::string_view characterId);
	std::optional<PlayerSettings> LoadPlayerSettings(std::uint64_t steamId);
	bool SaveCharacterState(std::string_view characterId, std::uint64_t expectedRevision,
							std::string_view stateJson, std::uint64_t& newRevision);
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
