#include "postgres_adapter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace ow::adapters
{
namespace
{
struct pg_conn;
struct pg_result;
using PGconn = pg_conn;
using PGresult = pg_result;
using Oid = unsigned int;
using ConnectionStatus = int;
using ExecStatus = int;

constexpr ConnectionStatus ConnectionOk = 0;
constexpr ExecStatus TuplesOk = 2;

struct PgApi
{
	using ConnectDb = PGconn* (*)(const char*);
	using Finish = void (*)(PGconn*);
	using Status = ConnectionStatus (*)(const PGconn*);
	using ErrorMessage = char* (*)(const PGconn*);
	using ExecParams = PGresult* (*)(PGconn*, const char*, int, const Oid*, const char* const*, const int*,
									 const int*, int);
	using ResultStatus = ExecStatus (*)(const PGresult*);
	using ResultErrorMessage = char* (*)(const PGresult*);
	using Tuples = int (*)(const PGresult*);
	using Fields = int (*)(const PGresult*);
	using Value = char* (*)(const PGresult*, int, int);
	using IsNull = int (*)(const PGresult*, int, int);
	using Clear = void (*)(PGresult*);

	ConnectDb connectDb {};
	Finish finish {};
	Status status {};
	ErrorMessage errorMessage {};
	ExecParams execParams {};
	ResultStatus resultStatus {};
	ResultErrorMessage resultErrorMessage {};
	Tuples tuples {};
	Fields fields {};
	Value value {};
	IsNull isNull {};
	Clear clear {};

#ifdef _WIN32
	HMODULE library {};
#else
	void* library {};
#endif

	~PgApi()
	{
#ifdef _WIN32
		if (library)
		{
			FreeLibrary(library);
		}
#else
		if (library)
		{
			dlclose(library);
		}
#endif
	}

	bool Load(std::string& error)
	{
#ifdef _WIN32
		const std::array<const char*, 3> names{"libpq.dll", "libpq-5.dll", "libpq"};
		for (const char* name : names)
		{
			library = LoadLibraryA(name);
			if (library)
			{
				break;
			}
		}
		if (!library)
		{
			error = "Could not load libpq.dll. Install PostgreSQL client tools or add its bin directory to PATH.";
			return false;
		}
#else
		const std::array<const char*, 2> names{"libpq.so.5", "libpq.so"};
		for (const char* name : names)
		{
			library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
			if (library)
			{
				break;
			}
		}
		if (!library)
		{
			error = "Could not load libpq.so. Install the PostgreSQL client library.";
			return false;
		}
#endif

		if (!LoadSymbol(connectDb, "PQconnectdb") || !LoadSymbol(finish, "PQfinish") ||
			!LoadSymbol(status, "PQstatus") || !LoadSymbol(errorMessage, "PQerrorMessage") ||
			!LoadSymbol(execParams, "PQexecParams") || !LoadSymbol(resultStatus, "PQresultStatus") ||
			!LoadSymbol(resultErrorMessage, "PQresultErrorMessage") || !LoadSymbol(tuples, "PQntuples") ||
			!LoadSymbol(fields, "PQnfields") || !LoadSymbol(value, "PQgetvalue") ||
			!LoadSymbol(isNull, "PQgetisnull") || !LoadSymbol(clear, "PQclear"))
		{
			error = "The loaded libpq is missing a required client API function.";
			return false;
		}
		return true;
	}

private:
	template <typename Function>
	bool LoadSymbol(Function& function, const char* name)
	{
#ifdef _WIN32
		function = reinterpret_cast<Function>(GetProcAddress(library, name));
#else
		function = reinterpret_cast<Function>(dlsym(library, name));
#endif
		return function != nullptr;
	}
};

bool ParseUnsigned(std::string_view text, std::uint64_t& result)
{
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
	return parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size();
}

bool ParseSigned(std::string_view text, std::int64_t& result)
{
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
	return parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size();
}

bool HasNul(std::string_view text)
{
	return text.find('\0') != std::string_view::npos;
}
} // namespace

struct PostgreSQLAdapter::Impl
{
	std::unique_ptr<PgApi> api;
	PGconn* connection {};
	mutable std::mutex mutex;
	std::string lastError;
	PlayerSettingsCache settingsCache;

	~Impl()
	{
		if (connection && api)
		{
			api->finish(connection);
		}
	}

	void SetError(std::string message) { lastError = std::move(message); }

	PGresult* Execute(std::string_view sql, const std::vector<std::string>& parameters)
	{
		if (!connection || api->status(connection) != ConnectionOk)
		{
			SetError("PostgreSQL connection is not available.");
			return nullptr;
		}
		std::vector<const char*> values;
		values.reserve(parameters.size());
		for (const std::string& parameter : parameters)
		{
			values.push_back(parameter.c_str());
		}
		const std::string query(sql);
		PGresult* result =
			api->execParams(connection, query.c_str(), static_cast<int>(values.size()), nullptr,
							values.empty() ? nullptr : values.data(), nullptr, nullptr, 0);
		if (!result)
		{
			const char* message = api->errorMessage(connection);
			SetError(message ? message : "PostgreSQL query failed without an error message.");
			return nullptr;
		}
		const ExecStatus status = api->resultStatus(result);
		if (status != TuplesOk)
		{
			const char* message = api->resultErrorMessage(result);
			SetError(message ? message : "PostgreSQL returned an unexpected query status.");
			api->clear(result);
			return nullptr;
		}
		SetError({});
		return result;
	}

	std::optional<std::string> GetText(PGresult* result, int row, int column) const
	{
		if (api->isNull(result, row, column))
		{
			return std::nullopt;
		}
		const char* value = api->value(result, row, column);
		return value ? std::optional<std::string>(value) : std::nullopt;
	}
};

PostgreSQLAdapter::PostgreSQLAdapter() : impl(std::make_unique<Impl>()) {}

PostgreSQLAdapter::~PostgreSQLAdapter() = default;

bool PostgreSQLAdapter::Connect(std::string_view connectionInfo)
{
	std::lock_guard lock(impl->mutex);
	if (connectionInfo.empty() || connectionInfo.find('\0') != std::string_view::npos)
	{
		impl->SetError("PostgreSQL connection string is empty or invalid.");
		return false;
	}
	impl->settingsCache.Clear();
	if (!impl->api)
	{
		impl->api = std::make_unique<PgApi>();
		if (!impl->api->Load(impl->lastError))
		{
			impl->api.reset();
			return false;
		}
	}

	if (impl->connection)
	{
		impl->api->finish(impl->connection);
		impl->connection = nullptr;
	}
	const std::string parameters(connectionInfo);
	impl->connection = impl->api->connectDb(parameters.c_str());
	if (!impl->connection || impl->api->status(impl->connection) != ConnectionOk)
	{
		const char* message = impl->connection ? impl->api->errorMessage(impl->connection) : nullptr;
		impl->SetError(message ? message : "PostgreSQL connection could not be established.");
		if (impl->connection)
		{
			impl->api->finish(impl->connection);
			impl->connection = nullptr;
		}
		return false;
	}
	impl->SetError({});
	return true;
}

bool PostgreSQLAdapter::Ping()
{
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute("SELECT 1", {});
	if (!result)
	{
		return false;
	}
	const bool success = impl->api->tuples(result) == 1 && impl->api->fields(result) == 1 &&
						 impl->GetText(result, 0, 0) == std::optional<std::string>("1");
	impl->api->clear(result);
	if (!success)
	{
		impl->SetError("PostgreSQL health check returned an unexpected result.");
	}
	return success;
}

bool PostgreSQLAdapter::IsConnected() const
{
	std::lock_guard lock(impl->mutex);
	return impl->connection && impl->api && impl->api->status(impl->connection) == ConnectionOk;
}

std::string PostgreSQLAdapter::GetLastError() const
{
	std::lock_guard lock(impl->mutex);
	return impl->lastError;
}

std::optional<std::string> PostgreSQLAdapter::ResolvePlayerServerRoute(std::string_view environment,
																	   std::uint64_t steamId,
																	   std::string_view currentServerId,
																	   std::string_view lobbyServerId)
{
	if (environment.empty() || environment.size() > 64 || HasNul(environment) || steamId == 0 ||
		steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || currentServerId.empty() ||
		currentServerId.size() > 64 || HasNul(currentServerId) || lobbyServerId.empty() || lobbyServerId.size() > 64 ||
		HasNul(lobbyServerId))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Player server route parameters are invalid.");
		return std::nullopt;
	}

	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT ow.resolve_player_server_route($1::text, $2::bigint, $3::text, $4::text)",
		{std::string(environment), std::to_string(steamId), std::string(currentServerId), std::string(lobbyServerId)});
	if (!result)
	{
		return std::nullopt;
	}
	std::optional<std::string> serverId;
	if (impl->api->tuples(result) == 1 && impl->api->fields(result) == 1)
	{
		serverId = impl->GetText(result, 0, 0);
	}
	impl->api->clear(result);
	if (!serverId || serverId->empty())
	{
		impl->SetError("PostgreSQL did not return the player's assigned server.");
		return std::nullopt;
	}
	return serverId;
}

std::optional<std::string> PostgreSQLAdapter::AssignPlayerServerRoute(std::string_view environment,
																	  std::uint64_t steamId,
																	  std::string_view destinationServerId)
{
	if (environment.empty() || environment.size() > 64 || HasNul(environment) || steamId == 0 ||
		steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
		destinationServerId.empty() || destinationServerId.size() > 64 || HasNul(destinationServerId))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Player server route parameters are invalid.");
		return std::nullopt;
	}

	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT ow.assign_player_server_route($1::text, $2::bigint, $3::text)",
		{std::string(environment), std::to_string(steamId), std::string(destinationServerId)});
	if (!result)
	{
		return std::nullopt;
	}
	std::optional<std::string> serverId;
	if (impl->api->tuples(result) == 1 && impl->api->fields(result) == 1)
	{
		serverId = impl->GetText(result, 0, 0);
	}
	impl->api->clear(result);
	if (!serverId || *serverId != destinationServerId)
	{
		impl->SetError("PostgreSQL did not confirm the requested player server assignment.");
		return std::nullopt;
	}
	return serverId;
}

std::optional<std::string> PostgreSQLAdapter::CreateCharacter(std::uint64_t steamId, std::string_view displayName)
{
	if (steamId == 0 || steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || displayName.empty() ||
		displayName.size() > 64 || HasNul(displayName))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Steam ID or character name is invalid.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result =
		impl->Execute("SELECT ow.create_character($1::bigint, $2::text)::text", {std::to_string(steamId),
																				 std::string(displayName)});
	if (!result)
	{
		return std::nullopt;
	}
	std::optional<std::string> id;
	if (impl->api->tuples(result) == 1 && impl->api->fields(result) == 1)
	{
		id = impl->GetText(result, 0, 0);
	}
	impl->api->clear(result);
	if (!id)
	{
		impl->SetError("PostgreSQL did not return the created character identifier.");
	}
	return id;
}

std::optional<std::vector<CharacterSummary>> PostgreSQLAdapter::ListCharacters(std::uint64_t steamId)
{
	if (steamId == 0 || steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Steam ID must be positive.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT character_id::text, display_name FROM ow.characters WHERE steam_id = $1::bigint "
		"ORDER BY created_at ASC, character_id ASC",
		{std::to_string(steamId)});
	if (!result)
	{
		return std::nullopt;
	}
	if (impl->api->fields(result) != 2)
	{
		impl->api->clear(result);
		impl->SetError("Character list query returned an invalid row shape.");
		return std::nullopt;
	}

	std::vector<CharacterSummary> characters;
	const int rowCount = impl->api->tuples(result);
	characters.reserve(static_cast<std::size_t>(rowCount));
	for (int row = 0; row < rowCount; ++row)
	{
		const auto characterId = impl->GetText(result, row, 0);
		const auto displayName = impl->GetText(result, row, 1);
		if (!characterId || !displayName)
		{
			impl->api->clear(result);
			impl->SetError("Character list contains a NULL field.");
			return std::nullopt;
		}
		characters.push_back({*characterId, *displayName});
	}
	impl->api->clear(result);
	return characters;
}

std::optional<CharacterData> PostgreSQLAdapter::LoadCharacter(std::string_view characterId)
{
	if (characterId.empty() || HasNul(characterId))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Character identifier is invalid.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT c.character_id::text, c.steam_id::text, c.display_name, c.state::text, c.revision::text, "
		"i.items::text, i.revision::text, e.balance::text, e.revision::text "
		"FROM ow.characters AS c "
		"JOIN ow.inventories AS i USING (character_id) "
		"JOIN ow.economy_accounts AS e USING (character_id) "
		"WHERE c.character_id = $1::uuid",
		{std::string(characterId)});
	if (!result)
	{
		return std::nullopt;
	}
	if (impl->api->tuples(result) != 1 || impl->api->fields(result) != 9)
	{
		impl->api->clear(result);
		impl->SetError("Character was not found or returned an invalid row.");
		return std::nullopt;
	}

	std::array<std::optional<std::string>, 9> fields;
	for (int column = 0; column < 9; ++column)
	{
		fields[static_cast<std::size_t>(column)] = impl->GetText(result, 0, column);
	}
	impl->api->clear(result);
	if (std::any_of(fields.begin(), fields.end(), [](const auto& field) { return !field.has_value(); }))
	{
		impl->SetError("Character row contains a NULL field.");
		return std::nullopt;
	}

	CharacterData character;
	character.characterId = *fields[0];
	if (!ParseUnsigned(*fields[1], character.steamId) || !ParseUnsigned(*fields[4], character.revision) ||
		!ParseUnsigned(*fields[6], character.inventoryRevision) ||
		!ParseSigned(*fields[7], character.balance) || !ParseUnsigned(*fields[8], character.balanceRevision))
	{
		impl->SetError("Character row contains an invalid numeric value.");
		return std::nullopt;
	}
	character.displayName = *fields[2];
	character.stateJson = *fields[3];
	character.inventoryJson = *fields[5];
	return character;
}

std::optional<CharacterData> PostgreSQLAdapter::LoadCharacterForPlayer(std::uint64_t steamId,
																			 std::string_view characterId)
{
	if (steamId == 0 || steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Steam ID must be positive.");
		return std::nullopt;
	}
	std::optional<CharacterData> character = LoadCharacter(characterId);
	if (character && character->steamId != steamId)
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Character does not belong to the authenticated player.");
		return std::nullopt;
	}
	return character;
}

std::optional<InventoryData> PostgreSQLAdapter::LoadInventory(std::string_view characterId)
{
	if (characterId.empty() || HasNul(characterId))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Character identifier is invalid.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT revision::text, items::text FROM ow.inventories WHERE character_id = $1::uuid",
		{std::string(characterId)});
	if (!result)
	{
		return std::nullopt;
	}
	if (impl->api->tuples(result) != 1 || impl->api->fields(result) != 2)
	{
		impl->api->clear(result);
		impl->SetError("Inventory was not found or returned an invalid row.");
		return std::nullopt;
	}
	const auto revisionText = impl->GetText(result, 0, 0);
	const auto itemsJson = impl->GetText(result, 0, 1);
	impl->api->clear(result);
	InventoryData inventory;
	if (!revisionText || !itemsJson || !ParseUnsigned(*revisionText, inventory.revision))
	{
		impl->SetError("Inventory row contains invalid data.");
		return std::nullopt;
	}
	inventory.itemsJson = *itemsJson;
	return inventory;
}

std::optional<PlayerSettings> PostgreSQLAdapter::LoadPlayerSettings(std::uint64_t steamId)
{
	if (steamId == 0 || steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Steam ID must be positive.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	if (!impl->connection || !impl->api || impl->api->status(impl->connection) != ConnectionOk)
	{
		impl->SetError("PostgreSQL connection is not available.");
		return std::nullopt;
	}
	if (auto cached = impl->settingsCache.Get(steamId))
	{
		impl->SetError({});
		return cached;
	}
	PGresult* result =
		impl->Execute("SELECT revision::text, settings::text FROM ow.player_settings WHERE steam_id = $1::bigint",
					  {std::to_string(steamId)});
	if (!result)
	{
		return std::nullopt;
	}
	if (impl->api->tuples(result) == 0)
	{
		impl->api->clear(result);
		impl->SetError({});
		PlayerSettings defaults{0, "{}"};
		impl->settingsCache.Put(steamId, defaults);
		return defaults;
	}
	if (impl->api->tuples(result) != 1 || impl->api->fields(result) != 2)
	{
		impl->api->clear(result);
		impl->SetError("Player settings query returned an invalid row.");
		return std::nullopt;
	}
	const auto revisionText = impl->GetText(result, 0, 0);
	const auto settings = impl->GetText(result, 0, 1);
	impl->api->clear(result);
	PlayerSettings loaded;
	if (!revisionText || !settings || !ParseUnsigned(*revisionText, loaded.revision))
	{
		impl->SetError("Player settings row contains invalid data.");
		return std::nullopt;
	}
	loaded.settingsJson = *settings;
	impl->settingsCache.Put(steamId, loaded);
	return loaded;
}

bool PostgreSQLAdapter::SaveCharacterState(std::string_view characterId, std::uint64_t expectedRevision,
										   std::string_view stateJson, std::uint64_t& newRevision)
{
	if (characterId.empty() || HasNul(characterId) || expectedRevision == 0 ||
		expectedRevision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || HasNul(stateJson))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Character state parameters are invalid.");
		return false;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute("SELECT ow.save_character_state($1::uuid, $2::bigint, $3::jsonb)::text",
									 {std::string(characterId), std::to_string(expectedRevision),
									  std::string(stateJson)});
	if (!result)
	{
		return false;
	}
	const auto revisionText = impl->api->tuples(result) == 1 && impl->api->fields(result) == 1
								  ? impl->GetText(result, 0, 0)
								  : std::nullopt;
	impl->api->clear(result);
	if (!revisionText || !ParseUnsigned(*revisionText, newRevision))
	{
		impl->SetError("PostgreSQL did not return the new character revision.");
		return false;
	}
	return true;
}

bool PostgreSQLAdapter::SaveInventory(std::string_view characterId, std::uint64_t expectedRevision,
									  std::string_view itemsJson, std::uint64_t& newRevision)
{
	if (characterId.empty() || HasNul(characterId) || expectedRevision == 0 ||
		expectedRevision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || HasNul(itemsJson))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Inventory parameters are invalid.");
		return false;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute("SELECT ow.save_inventory($1::uuid, $2::bigint, $3::jsonb)::text",
									 {std::string(characterId), std::to_string(expectedRevision), std::string(itemsJson)});
	if (!result)
	{
		return false;
	}
	const auto revisionText = impl->api->tuples(result) == 1 && impl->api->fields(result) == 1
								  ? impl->GetText(result, 0, 0)
								  : std::nullopt;
	impl->api->clear(result);
	if (!revisionText || !ParseUnsigned(*revisionText, newRevision))
	{
		impl->SetError("PostgreSQL did not return the new inventory revision.");
		return false;
	}
	return true;
}

bool PostgreSQLAdapter::SavePlayerSettings(std::uint64_t steamId, std::uint64_t expectedRevision,
										   std::string_view settingsJson, std::uint64_t& newRevision)
{
	if (steamId == 0 || steamId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
		expectedRevision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || HasNul(settingsJson))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Steam ID must be positive.");
		return false;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute("SELECT ow.save_player_settings($1::bigint, $2::bigint, $3::jsonb)::text",
									 {std::to_string(steamId), std::to_string(expectedRevision),
									  std::string(settingsJson)});
	if (!result)
	{
		impl->settingsCache.Invalidate(steamId);
		return false;
	}
	const auto revisionText = impl->api->tuples(result) == 1 && impl->api->fields(result) == 1
								  ? impl->GetText(result, 0, 0)
								  : std::nullopt;
	impl->api->clear(result);
	if (!revisionText || !ParseUnsigned(*revisionText, newRevision))
	{
		impl->SetError("PostgreSQL did not return the new settings revision.");
		impl->settingsCache.Invalidate(steamId);
		return false;
	}
	impl->settingsCache.Put(steamId, PlayerSettings{newRevision, std::string(settingsJson)});
	return true;
}

std::optional<MoneyTransactionResult> PostgreSQLAdapter::ApplyMoneyTransaction(
	std::string_view characterId, std::string_view idempotencyKey, std::int64_t amount, std::string_view reason)
{
	if (characterId.empty() || HasNul(characterId) || idempotencyKey.empty() || idempotencyKey.size() > 128 ||
		HasNul(idempotencyKey) || amount == 0 || reason.empty() || reason.size() > 128 || HasNul(reason))
	{
		std::lock_guard lock(impl->mutex);
		impl->SetError("Money transaction parameters are invalid.");
		return std::nullopt;
	}
	std::lock_guard lock(impl->mutex);
	PGresult* result = impl->Execute(
		"SELECT new_balance::text, CASE WHEN was_duplicate THEN 'true' ELSE 'false' END "
		"FROM ow.apply_money_transaction($1::uuid, $2::text, $3::bigint, $4::text)",
		{std::string(characterId), std::string(idempotencyKey), std::to_string(amount), std::string(reason)});
	if (!result)
	{
		return std::nullopt;
	}
	if (impl->api->tuples(result) != 1 || impl->api->fields(result) != 2)
	{
		impl->api->clear(result);
		impl->SetError("PostgreSQL returned an invalid money transaction result.");
		return std::nullopt;
	}
	const auto balanceText = impl->GetText(result, 0, 0);
	const auto duplicateText = impl->GetText(result, 0, 1);
	impl->api->clear(result);
	MoneyTransactionResult transaction;
	if (!balanceText || !duplicateText || !ParseSigned(*balanceText, transaction.balance) ||
		(*duplicateText != "true" && *duplicateText != "false"))
	{
		impl->SetError("PostgreSQL returned malformed money transaction data.");
		return std::nullopt;
	}
	transaction.duplicate = *duplicateText == "true";
	return transaction;
}
} // namespace ow::adapters
