#include "adapters/cs2/client_redirector.h"
#include "adapters/persistence/player_settings_cache.h"
#include "adapters/persistence/postgres_adapter.h"
#include "core/cluster_config.h"
#include "core/event_bus.h"
#include "core/module_manager.h"
#include "core/service_registry.h"
#include "modules/persistence/player_character_worker.h"
#include "modules/persistence/player_route_worker.h"
#include "modules/persistence/player_settings_worker.h"
#ifdef OW_ENABLE_CACHE_FALLBACK
#include "modules/persistence/cache_manager.h"
#include "modules/persistence/player_warmup.h"
#include "modules/persistence/redis_cache.h"
#endif
#include "modules/economy/economy_system.h"
#include "modules/inventory/inventory_system.h"
#include "modules/transfer/transfer_manager.h"
#include "modules/transfer/transfer_module.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
struct TestEvent
{
	int value {};
};

struct TestService
{
	virtual ~TestService() = default;
	virtual int GetValue() const = 0;
};

struct TestServiceImpl final : TestService
{
	int GetValue() const override { return 7; }
};

struct TestModule final : ow::core::IModule
{
	TestModule(std::string moduleId, std::vector<std::string> moduleDependencies, std::vector<std::string>* callLog)
		: id(std::move(moduleId)), dependencies(std::move(moduleDependencies)), calls(callLog)
	{
	}

	std::string id;
	std::vector<std::string> dependencies;
	std::vector<std::string>* calls {};
	bool initializeSucceeds {true};
	bool startSucceeds {true};

	std::string GetId() const override { return id; }
	std::vector<std::string> GetDependencies() const override { return dependencies; }
	bool Initialize() override
	{
		calls->push_back("init:" + id);
		return initializeSucceeds;
	}
	bool Start() override
	{
		calls->push_back("start:" + id);
		return startSucceeds;
	}
	void Stop() override { calls->push_back("stop:" + id); }
};

ow::transfer::TransferRecord MakeTransfer(std::string id, std::string idempotencyKey,
										  std::chrono::system_clock::time_point now)
{
	ow::transfer::TransferRecord transfer;
	transfer.transferId = std::move(id);
	transfer.idempotencyKey = std::move(idempotencyKey);
	transfer.transferToken = "one-time-token";
	transfer.snapshot.steamId = 76561198000000000ull;
	transfer.snapshot.characterId = "character-1";
	transfer.snapshot.sourceServerId = "source-01";
	transfer.snapshot.destinationServerId = "destination-01";
	transfer.snapshot.dataRevision = 4;
	transfer.expiresAt = now + std::chrono::minutes(1);
	return transfer;
}

bool TestModuleManager()
{
	std::vector<std::string> calls;
	TestModule dependent {"dependent", {"base"}, &calls};
	TestModule base {"base", {}, &calls};
	ow::core::ModuleManager manager;
	if (!manager.RegisterModule(dependent) || !manager.RegisterModule(base) || !manager.StartAll() || !manager.IsRunning())
	{
		return false;
	}

	const std::vector<std::string> expected {"init:base", "start:base", "init:dependent", "start:dependent"};
	if (calls != expected)
	{
		return false;
	}

	manager.StopAll();
	if (calls.size() != 6 || calls[4] != "stop:dependent" || calls[5] != "stop:base")
	{
		return false;
	}

	std::vector<std::string> reloadCalls;
	TestModule reloaded {"reloaded", {}, &reloadCalls};
	manager.Clear();
	if (!manager.RegisterModule(reloaded) || !manager.StartAll() || !manager.IsRunning())
	{
		return false;
	}
	manager.Clear();
	if (reloadCalls.size() != 3 || reloadCalls.back() != "stop:reloaded")
	{
		return false;
	}

	std::vector<std::string> failedCalls;
	TestModule failing {"failing", {}, &failedCalls};
	failing.startSucceeds = false;
	ow::core::ModuleManager failingManager;
	return failingManager.RegisterModule(failing) && !failingManager.StartAll() && !failingManager.IsRunning()
		   && failingManager.GetLastError() == "Module start failed: failing" && failedCalls.size() == 3 && failedCalls.back() == "stop:failing";
}

bool TestEventBus()
{
	ow::core::EventBus bus;
	int calls = 0;
	ow::core::EventBus::SubscriptionId secondSubscription = 0;
	bus.Subscribe<TestEvent>([&](const TestEvent& event) {
		calls += event.value;
		bus.Unsubscribe(secondSubscription);
	});
	secondSubscription = bus.Subscribe<TestEvent>([&](const TestEvent&) { calls += 100; });
	bus.Publish(TestEvent{2});
	return calls == 2 && !bus.Unsubscribe(secondSubscription);
}

bool TestServiceRegistry()
{
	ow::core::ServiceRegistry registry;
	TestServiceImpl service;
	return registry.Register<TestService>(service, 2) && registry.Get<TestService>(2) == &service &&
		   registry.Get<TestService>(3) == nullptr && registry.Unregister<TestService>(service) &&
		   registry.Get<TestService>() == nullptr;
}

#ifdef OW_ENABLE_CACHE_FALLBACK
bool TestMemoryCache()
{
	ow::persistence::MemoryCache cache;
	if (!cache.Put("character:1", "state-v4", 4, std::chrono::seconds(10)))
	{
		return false;
	}
	if (cache.Put("character:1", "stale", 3, std::chrono::seconds(10)))
	{
		return false;
	}
	const auto current = cache.Get("character:1", 4);
	if (!current || current->value != "state-v4" || current->revision != 4 ||
		cache.Get("character:1", 5).has_value())
	{
		return false;
	}
	if (!cache.Invalidate("character:1") || cache.Get("character:1").has_value())
	{
		return false;
	}

	if (cache.Put("expired:entry", "old", 10, std::chrono::seconds(-1)))
	{
		return false;
	}

	ow::persistence::MemoryCache boundedCache(2, 12);
	if (!boundedCache.Put("a", "value", 1, std::chrono::seconds(10)) ||
		!boundedCache.Put("b", "value", 1, std::chrono::seconds(10)) ||
		!boundedCache.Get("a") ||
		!boundedCache.Put("c", "value", 1, std::chrono::seconds(10)))
	{
		return false;
	}
	return boundedCache.Size() == 2 && boundedCache.BytesInUse() <= 12 && boundedCache.Get("a").has_value() &&
		   !boundedCache.Get("b").has_value() && boundedCache.Get("c").has_value() &&
		   !boundedCache.Put("oversized", "value", 1, std::chrono::seconds(10));
}

bool TestPlayerWarmup()
{
	using namespace ow::persistence;
	MemoryCache cache;
	AsyncCacheWarmer warmer(cache, 2, 8);
	PlayerWarmupPlan plan;
	std::promise<void> releaseDeferred;
	const std::shared_future<void> deferredGate = releaseDeferred.get_future().share();
	std::atomic<bool> deferredStarted {};

	if (!plan.AddRequired("player:42:transfer-state", 8, std::chrono::seconds(30), [] {
			return std::optional<CacheLoadValue>{{"transfer-snapshot-v8", 8}};
		}) ||
		!plan.AddDeferred("player:42:settings", 3, std::chrono::seconds(60), [&] {
			deferredStarted.store(true, std::memory_order_release);
			deferredGate.wait();
			return std::optional<CacheLoadValue>{{"settings-v3", 3}};
		}) ||
		!plan.AddDeferred("player:42:cosmetics", 2, std::chrono::seconds(60), [] {
			return std::optional<CacheLoadValue>{{"cosmetics-v2", 2}};
		}))
	{
		return false;
	}

	const bool started = plan.Start(warmer);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (started && !plan.CanPlay() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const bool playableBeforeDeferredFinish =
		plan.CanSpawn() && plan.CanPlay() && !plan.IsFullyWarm() && plan.GetPendingCount() > 0;
	releaseDeferred.set_value();

	const auto fullyWarmDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (started && !plan.IsFullyWarm() && std::chrono::steady_clock::now() < fullyWarmDeadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto transferState = cache.Get("player:42:transfer-state", 8);
	const auto settings = cache.Get("player:42:settings", 3);
	const auto cosmetics = cache.Get("player:42:cosmetics", 2);
	if (!started || !playableBeforeDeferredFinish || !deferredStarted.load(std::memory_order_acquire) ||
		!plan.IsFullyWarm() || !transferState || transferState->value != "transfer-snapshot-v8" || !settings ||
		settings->value != "settings-v3" || !cosmetics || cosmetics->value != "cosmetics-v2")
	{
		return false;
	}

	MemoryCache staleCache;
	AsyncCacheWarmer staleWarmer(staleCache, 1, 4);
	PlayerWarmupPlan stalePlan;
	if (!stalePlan.AddRequired("player:42:stale-critical", 4, std::chrono::seconds(30),
							   [] { return std::optional<CacheLoadValue> {{"outdated", 3}}; })
		|| !stalePlan.Start(staleWarmer))
	{
		return false;
	}
	const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (stalePlan.GetFailures().empty() && std::chrono::steady_clock::now() < staleDeadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return !stalePlan.CanPlay() && stalePlan.GetFailures().size() == 1;
}
#endif

bool TestClusterConfig()
{
	const std::string validConfig = R"(
[core]
server_id = a
environment = test
enabled = 1
[persistence]
provider = postgresql
connection_env = OW_PG_TEST
[cache]
provider = redis
shared_provider = redis
redis_host = 127.0.0.1
redis_port = 6379
redis_database = 1
redis_username_env = OW_REDIS_USER
redis_password_env = OW_REDIS_PASSWORD
redis_key_prefix = openworld:test:
redis_timeout_ms = 250
redis_max_value_bytes = 1048576
redis_tls = false
[routing]
enforce_server_affinity = true
lobby_server_id = a
[servers.a]
connect = 127.0.0.1:27015
map = map_a
max_players = 32
workshop_collection_id =
[servers.b]
connect = server.example.net:27016
map = map_b
max_players = 32
[zones.west]
server_id = a
map = map_a
min_x = -100
min_y = -100
max_x = 0
max_y = 100
[zones.east]
server_id = b
map = map_b
min_x = 0
min_y = -100
max_x = 100
max_y = 100
)";
	std::string error;
	auto config = ow::core::ClusterConfig::Parse(validConfig, &error);
	if (!config || !error.empty() || !config->CurrentServer() || !config->enabled || config->worldMode != ow::core::WorldMode::Cluster
		|| !config->adapters.postgresql || !config->adapters.panorama || !config->adapters.hud || !config->adapters.input
		|| !config->adapters.mapOverview || !config->modules.characters || !config->modules.economy || !config->modules.inventory
		|| !config->modules.transfer || !config->routing.enforceServerAffinity ||
		config->routing.lobbyServerId != "a" || !config->FindServer("b") ||
		config->FindServer("b")->connect != "server.example.net:27016"
		|| config->FindServer("missing"))
	{
		return false;
	}
	const auto* westZone = config->FindZone("map_a", -50, 0);
	const auto* eastZone = config->FindZone("map_b", 0, 0);
	if (!westZone || !eastZone || westZone->serverId != "a" || eastZone->serverId != "b" ||
		config->FindZone("map_a", 0, 0) != nullptr || config->FindZone("map_a", 100, 0) != nullptr)
	{
		return false;
	}
	const auto* transitionZone = config->FindTransitionZone("map_a", "a", 0, 0);
	if (config->FindTransitionZone("map_a", "a", -50, 0) != nullptr || !transitionZone ||
		transitionZone->serverId != "b" || config->FindTransitionZone("missing_map", "a", 0, 0) != nullptr ||
		config->FindTransitionZone("map_a", "a", 100, 0) != nullptr)
	{
		return false;
	}
	if (config->redisHost != "127.0.0.1" || config->redisPort != 6379 || config->redisDatabase != 1 || config->redisKeyPrefix != "openworld:test:"
		|| config->redisPasswordEnv != "OW_REDIS_PASSWORD")
	{
		return false;
	}

	const std::string overlappingConfig = validConfig + R"(
[zones.overlap]
server_id = b
map = map_a
min_x = -1
min_y = -1
max_x = 1
max_y = 1
)";
	if (ow::core::ClusterConfig::Parse(overlappingConfig, &error) || error.empty())
	{
		return false;
	}
	std::string unknownLobbyConfig = validConfig;
	const std::string configuredLobby = "lobby_server_id = a";
	const std::size_t lobbyPosition = unknownLobbyConfig.find(configuredLobby);
	unknownLobbyConfig.replace(lobbyPosition, configuredLobby.size(), "lobby_server_id = missing");
	if (ow::core::ClusterConfig::Parse(unknownLobbyConfig, &error) || error.empty())
	{
		return false;
	}

	std::string tlsConfig = validConfig;
	const std::string disabledTls = "redis_tls = false";
	const std::size_t tlsPosition = tlsConfig.find(disabledTls);
	tlsConfig.replace(tlsPosition, disabledTls.size(), "redis_tls = true");
	if (ow::core::ClusterConfig::Parse(tlsConfig, &error) || error.find("TLS") == std::string::npos)
	{
		return false;
	}

	const std::string singleMapConfig = R"(
[core]
server_id = local
environment = test
enabled = true
[persistence]
provider = none
connection_env =
[cache]
provider = memory
shared_provider = none
redis_key_prefix = openworld:test:
[world]
mode = single_map
map = de_dust2
[adapters]
postgresql = false
panorama = true
hud = false
input = true
map_overview = false
[modules]
characters = false
economy = false
inventory = false
transfer = false
[servers.local]
connect = 127.0.0.1:27015
map = de_dust2
max_players = 16
workshop_collection_id =
)";
	auto singleMap = ow::core::ClusterConfig::Parse(singleMapConfig, &error);
	if (!singleMap || !error.empty() || singleMap->worldMode != ow::core::WorldMode::SingleMap || singleMap->worldMap != "de_dust2"
		|| !singleMap->CurrentServer() || !singleMap->zones.empty() || singleMap->adapters.postgresql || singleMap->adapters.hud
		|| singleMap->adapters.mapOverview || !singleMap->adapters.panorama || !singleMap->adapters.input || singleMap->modules.characters
		|| singleMap->modules.economy || singleMap->modules.inventory || singleMap->modules.transfer)
	{
		return false;
	}

	std::string mismatchedMapConfig = singleMapConfig;
	const std::string matchingMap = "map = de_dust2";
	const std::size_t serverMapPosition = mismatchedMapConfig.rfind(matchingMap);
	mismatchedMapConfig.replace(serverMapPosition, matchingMap.size(), "map = de_mirage");
	if (ow::core::ClusterConfig::Parse(mismatchedMapConfig, &error) || error.empty())
	{
		return false;
	}

	std::string unsafeMapConfig = singleMapConfig;
	const std::string safeWorldMap = "map = de_dust2";
	const std::size_t worldMapPosition = unsafeMapConfig.find(safeWorldMap);
	unsafeMapConfig.replace(worldMapPosition, safeWorldMap.size(), "map = ../de_dust2");
	if (ow::core::ClusterConfig::Parse(unsafeMapConfig, &error) || error.empty())
	{
		return false;
	}

	std::string invalidPersistenceConfig = singleMapConfig;
	const std::string disabledEconomy = "economy = false";
	const std::size_t economyPosition = invalidPersistenceConfig.find(disabledEconomy);
	invalidPersistenceConfig.replace(economyPosition, disabledEconomy.size(), "economy = true");
	return !ow::core::ClusterConfig::Parse(invalidPersistenceConfig, &error) && !error.empty();
}

bool TestConfigExamples()
{
	const std::filesystem::path configDirectory = std::filesystem::path(OW_TEST_SOURCE_DIR) / "configs";
	std::string error;
	const auto clusterPath = configDirectory / "openworld.ini.example";
	const auto cluster = ow::core::ClusterConfig::Load(clusterPath.string(), &error);
	if (!cluster || !error.empty() || cluster->worldMode != ow::core::WorldMode::Cluster || !cluster->adapters.postgresql ||
		!cluster->modules.characters || !cluster->modules.transfer || !cluster->routing.enforceServerAffinity ||
		cluster->routing.lobbyServerId != "server-01")
	{
		std::fprintf(stderr, "Could not load %s: %s\n", clusterPath.string().c_str(), error.c_str());
		return false;
	}
	const auto singleMapPath = configDirectory / "openworld.single-map.ini.example";
	const auto singleMap = ow::core::ClusterConfig::Load(singleMapPath.string(), &error);
	if (!singleMap || !error.empty() || singleMap->worldMode != ow::core::WorldMode::SingleMap ||
		singleMap->worldMap != "de_dust2" || singleMap->adapters.postgresql || singleMap->modules.inventory)
	{
		std::fprintf(stderr, "Could not load %s: %s\n", singleMapPath.string().c_str(), error.c_str());
		return false;
	}
	return true;
}

#ifdef OW_ENABLE_CACHE_FALLBACK
bool TestRedisCacheFailures()
{
	ow::persistence::RedisCacheConfig config;
	config.host = "127.0.0.1";
	config.port = 1;
	config.timeout = std::chrono::milliseconds(100);
	ow::persistence::RedisCache cache(std::move(config));
	return !cache.Ping() && !cache.GetLastError().empty() && !cache.Put("key", "value", 1, std::chrono::seconds(1));
}

bool TestRedisCacheIntegrationWhenConfigured()
{
	const char* portText = std::getenv("OW_TEST_REDIS_PORT");
	if (!portText || portText[0] == '\0')
	{
		return true;
	}
	unsigned int port {};
	const std::string portValue(portText);
	const auto converted = std::from_chars(portValue.data(), portValue.data() + portValue.size(), port);
	if (converted.ec != std::errc {} || converted.ptr != portValue.data() + portValue.size() || port == 0 || port > 65535)
	{
		return false;
	}

	ow::persistence::RedisCacheConfig config;
	config.host = std::getenv("OW_TEST_REDIS_HOST") ? std::getenv("OW_TEST_REDIS_HOST") : "127.0.0.1";
	config.port = static_cast<std::uint16_t>(port);
	config.username = std::getenv("OW_TEST_REDIS_USERNAME") ? std::getenv("OW_TEST_REDIS_USERNAME") : "";
	config.password = std::getenv("OW_TEST_REDIS_PASSWORD") ? std::getenv("OW_TEST_REDIS_PASSWORD") : "";
	config.keyPrefix = "openworld:adapter-test:";
	config.timeout = std::chrono::milliseconds(500);
	ow::persistence::RedisCache cache(std::move(config));
	const std::string key = "wire-format-roundtrip";
	const std::string payload("nul\0newline\npayload", 19);
	cache.Invalidate(key);
	if (!cache.Ping() || !cache.Put(key, payload, 9007199254740993ull, std::chrono::seconds(30)))
	{
		return false;
	}
	const auto loaded = cache.Get(key, 9007199254740993ull);
	const bool readMatches = loaded && loaded->revision == 9007199254740993ull && loaded->value == payload;
	const bool staleWriteRejected =
		!cache.Put(key, "stale", 9007199254740992ull, std::chrono::seconds(30));
	const auto stillCurrent = cache.Get(key, 9007199254740993ull);
	const bool revisionPreserved = stillCurrent && stillCurrent->value == payload;
	const bool invalidated = cache.Invalidate(key) && !cache.Get(key).has_value();
	return readMatches && staleWriteRejected && revisionPreserved && invalidated;
}
#endif

bool TestPostgreSQLAdapterInputValidation()
{
	ow::adapters::PostgreSQLAdapter adapter;
	if (adapter.Connect("") || adapter.GetLastError().empty())
	{
		return false;
	}
	std::uint64_t newRevision {};
	return !adapter.SaveCharacterState("", 0, "{}", newRevision) && !adapter.GetLastError().empty() &&
		   !adapter.ListCharacters(0).has_value() && !adapter.GetLastError().empty() &&
		   !adapter.LoadCharacterForPlayer(0, "character-1").has_value() && !adapter.GetLastError().empty() &&
		   !adapter.LoadInventory("").has_value() && !adapter.GetLastError().empty() &&
		   !adapter.SaveInventory("", 0, "[]", newRevision) && !adapter.GetLastError().empty() &&
		   !adapter.SavePlayerSettings(0, 0, "{}", newRevision) && !adapter.GetLastError().empty() &&
		   !adapter.ApplyMoneyTransaction("", "", 0, "").has_value() && !adapter.GetLastError().empty();
}

bool TestPlayerSettingsCache()
{
	using namespace std::chrono_literals;
	ow::adapters::PlayerSettingsCache cache(2, 32, 1s);
	const std::string firstSettings = R"({"volume":0.5})";
	if (!cache.Put(10, {2, firstSettings}))
	{
		return false;
	}
	if (!cache.Put(9, {0, "{}"}))
	{
		return false;
	}
	const auto defaults = cache.Get(9);
	if (!defaults || defaults->revision != 0 || defaults->settingsJson != "{}")
	{
		return false;
	}
	const auto loaded = cache.Get(10);
	if (!loaded || loaded->revision != 2 || loaded->settingsJson != firstSettings ||
		cache.Size() != 2 || cache.BytesInUse() != firstSettings.size() + 2 ||
		cache.Put(10, {1, R"({"volume":0.1})"}))
	{
		return false;
	}
	if (!cache.Put(11, {1, R"({"theme":"dark"})"}) || !cache.Get(10) ||
		!cache.Put(12, {1, R"({"theme":"light"})"}) || cache.Get(11).has_value() ||
		!cache.Invalidate(12) || cache.Invalidate(12) ||
		cache.Put(13, {1, std::string(33, 'x')}))
	{
		return false;
	}
	cache.Clear();
	if (cache.Size() != 0 || cache.BytesInUse() != 0 || cache.Get(0))
	{
		return false;
	}

	ow::adapters::PlayerSettingsCache expiringCache(1, 32, 1ms);
	if (!expiringCache.Put(20, {1, "{}"}))
	{
		return false;
	}
	std::this_thread::sleep_for(2ms);
	return !expiringCache.Get(20).has_value();
}

bool TestPlayerSettingsWorker()
{
	using namespace std::chrono_literals;
	std::atomic<int> loaderCalls {};
	ow::persistence::PlayerSettingsWorker worker(
		[&loaderCalls](std::uint64_t steamId, std::string& error)
			-> std::optional<ow::adapters::PlayerSettings> {
			++loaderCalls;
			std::this_thread::sleep_for(10ms);
			if (steamId == 22)
			{
				error = "test load failure";
				return std::nullopt;
			}
			return ow::adapters::PlayerSettings{3, R"({"ready":true})"};
		},
		2);
	if (!worker.Request(1001, 21) || !worker.Request(1002, 22) || worker.Request(1003, 23) ||
		worker.Request(0, 23) || worker.Request(1004, 0))
	{
		return false;
	}

	std::optional<ow::persistence::PlayerSettingsLoadResult> first;
	std::optional<ow::persistence::PlayerSettingsLoadResult> second;
	const auto deadline = std::chrono::steady_clock::now() + 1s;
	while (std::chrono::steady_clock::now() < deadline && (!first || !second))
	{
		auto result = worker.TryTakeResult();
		if (result)
		{
			if (result->requestId == 1001)
			{
				first = std::move(result);
			}
			else if (result->requestId == 1002)
			{
				second = std::move(result);
			}
			else
			{
				return false;
			}
		}
		else
		{
			std::this_thread::sleep_for(1ms);
		}
	}
	return first && first->steamId == 21 && first->settings && first->settings->revision == 3 &&
		   first->settings->settingsJson == R"({"ready":true})" && first->error.empty() && second &&
		   second->steamId == 22 && !second->settings && second->error == "test load failure" &&
		   loaderCalls.load() == 2;
}

bool TestPlayerCharacterWorker()
{
	using namespace std::chrono_literals;
	std::atomic<int> loaderCalls {};
	ow::persistence::PlayerCharacterWorker worker(
		[&loaderCalls](const ow::persistence::PlayerCharacterJob& job,
					   ow::persistence::PlayerCharacterResult& result) {
			++loaderCalls;
			std::this_thread::sleep_for(5ms);
			if (job.operation == ow::persistence::PlayerCharacterOperation::List)
			{
				result.characters.push_back({"character-1", "Test Character"});
				return;
			}
			ow::adapters::CharacterData character;
			character.characterId = job.operation == ow::persistence::PlayerCharacterOperation::Create ? "created-id" : job.value;
			character.steamId = job.steamId;
			character.displayName = job.operation == ow::persistence::PlayerCharacterOperation::Create ? job.value : "Loaded";
			result.character = std::move(character);
		},
		3);
	if (!worker.RequestList(2001, 31) || !worker.RequestCreate(2002, 31, "New Hero") ||
		!worker.RequestLoad(2003, 31, "character-1") || worker.RequestCreate(2004, 31, "") ||
		worker.RequestLoad(2005, 31, "") || worker.RequestList(0, 31) || worker.RequestList(2006, 31))
	{
		return false;
	}

	std::vector<ow::persistence::PlayerCharacterResult> results;
	const auto deadline = std::chrono::steady_clock::now() + 1s;
	while (std::chrono::steady_clock::now() < deadline && results.size() < 3)
	{
		if (auto result = worker.TryTakeResult())
		{
			results.push_back(std::move(*result));
		}
		else
		{
			std::this_thread::sleep_for(1ms);
		}
	}
	if (results.size() != 3 || loaderCalls.load() != 3)
	{
		return false;
	}
	const auto findResult = [&results](std::uint64_t requestId) -> const ow::persistence::PlayerCharacterResult* {
		const auto result = std::find_if(results.begin(), results.end(), [requestId](const auto& entry) {
			return entry.requestId == requestId;
		});
		return result == results.end() ? nullptr : &*result;
	};
	const auto* listed = findResult(2001);
	const auto* created = findResult(2002);
	const auto* loaded = findResult(2003);
	return listed && listed->steamId == 31 && listed->characters.size() == 1 &&
		   listed->characters[0].characterId == "character-1" && created && created->character &&
		   created->character->characterId == "created-id" && created->character->displayName == "New Hero" && loaded &&
		   loaded->character && loaded->character->characterId == "character-1" && loaded->error.empty();
}

bool TestPostgreSQLIntegrationWhenConfigured()
{
	const char* connectionInfo = std::getenv("OW_TEST_PG_CONNINFO");
	if (!connectionInfo || connectionInfo[0] == '\0')
	{
		return true;
	}

	ow::adapters::PostgreSQLAdapter adapter;
	if (!adapter.Connect(connectionInfo) || !adapter.Ping())
	{
		std::fprintf(stderr, "PostgreSQL integration setup failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}

	const auto uniquePart = static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() % 9000000000ull);
	const std::uint64_t steamId = 76561197960265728ull + uniquePart;
	const auto characterId = adapter.CreateCharacter(steamId, "OW adapter integration test");
	if (!characterId)
	{
		std::fprintf(stderr, "PostgreSQL character creation failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	const auto characters = adapter.ListCharacters(steamId);
	const auto ownedCharacter = adapter.LoadCharacterForPlayer(steamId, *characterId);
	if (!characters || characters->size() != 1 || characters->front().characterId != *characterId ||
		!ownedCharacter || adapter.LoadCharacterForPlayer(steamId + 1, *characterId))
	{
		std::fprintf(stderr, "PostgreSQL character ownership/list check failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	const auto defaultSettings = adapter.LoadPlayerSettings(steamId);
	if (!defaultSettings || defaultSettings->revision != 0 || defaultSettings->settingsJson != "{}")
	{
		std::fprintf(stderr, "PostgreSQL default settings load failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	auto character = adapter.LoadCharacter(*characterId);
	if (!character || character->steamId != steamId || character->revision != 1 || character->balance != 0)
	{
		std::fprintf(stderr, "PostgreSQL character load failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}

	auto inventory = adapter.LoadInventory(*characterId);
	if (!inventory || inventory->revision != 1 || inventory->itemsJson != "[]")
	{
		std::fprintf(stderr, "PostgreSQL default inventory load failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	std::uint64_t inventoryRevision {};
	if (!adapter.SaveInventory(*characterId, inventory->revision, R"([{"item":"integration_test"}])", inventoryRevision) ||
		inventoryRevision != 2 || adapter.SaveInventory(*characterId, inventory->revision, "[]", inventoryRevision))
	{
		std::fprintf(stderr, "PostgreSQL inventory save/revision check failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	inventory = adapter.LoadInventory(*characterId);
	if (!inventory || inventory->revision != 2 || inventory->itemsJson.find("integration_test") == std::string::npos)
	{
		std::fprintf(stderr, "PostgreSQL inventory reload failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}

	std::uint64_t newRevision {};
	if (!adapter.SaveCharacterState(*characterId, character->revision, R"({"integration_test":true})", newRevision) ||
		newRevision != 2 || adapter.SaveCharacterState(*characterId, character->revision, "{}", newRevision))
	{
		std::fprintf(stderr, "PostgreSQL character state save/revision check failed: %s\n",
					 adapter.GetLastError().c_str());
		return false;
	}

	if (!adapter.SavePlayerSettings(steamId, 0, R"({"integration_test":true})", newRevision) || newRevision != 1)
	{
		std::fprintf(stderr, "PostgreSQL initial settings save failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}
	const auto settings = adapter.LoadPlayerSettings(steamId);
	if (!settings || settings->revision != 1 ||
		settings->settingsJson.find("integration_test") == std::string::npos)
	{
		std::fprintf(stderr, "PostgreSQL settings load failed: %s\n", adapter.GetLastError().c_str());
		return false;
	}

	ow::core::ServiceRegistry registry;
	ow::economy::EconomySystem economy(registry, &adapter);
	ow::core::ModuleManager modules;
	if (!modules.RegisterModule(economy) || !modules.StartAll())
	{
		return false;
	}
	auto* service = registry.Get<ow::economy::IEconomyService>();
	const auto wallet = service ? service->LoadWallet(*characterId) : std::nullopt;
	const std::string idempotencyKey = "ow-core-tests:" + *characterId;
	const auto credited = service ? service->ApplyTransaction(*characterId, idempotencyKey, 25, "Integration test")
								 : std::nullopt;
	const auto duplicate = service ? service->ApplyTransaction(*characterId, idempotencyKey, 25, "Integration test")
								  : std::nullopt;
		modules.StopAll();
	return wallet && wallet->balance == 0 && credited && duplicate && credited->balance == 25 && !credited->duplicate &&
		   duplicate->balance == 25 && duplicate->duplicate;
}

bool TestTransferModuleServiceLifecycle()
{
	using namespace ow::transfer;
	ow::core::ServiceRegistry registry;
	ow::core::EventBus events;
	ow::modules::TransferModule module(registry, events);
	ow::core::ModuleManager modules;
	if (!modules.RegisterModule(module) || registry.Get<ITransferService>() || !modules.StartAll())
	{
		return false;
	}

	ITransferService* service = registry.Get<ITransferService>(1);
	const auto now = std::chrono::system_clock::now();
	const auto transfer = MakeTransfer("module-transfer", "module-request", now);
	if (!service || registry.Get<ITransferService>(2) || !module.Start() ||
		!service->Request(transfer, true, now) || !service->Find("module-transfer"))
	{
		return false;
	}
	modules.StopAll();
	if (registry.Get<ITransferService>())
	{
		return false;
	}

	TransferManager incumbent;
	if (!registry.Register<ITransferService>(incumbent, 1))
	{
		return false;
	}
	ow::modules::TransferModule conflictingModule(registry, events);
	ow::core::ModuleManager conflictingModules;
	return conflictingModules.RegisterModule(conflictingModule) && !conflictingModules.StartAll() &&
		   registry.Get<ITransferService>() == &incumbent &&
		   registry.Unregister<ITransferService>(incumbent);
}

bool TestEconomyModuleServiceLifecycle()
{
	ow::core::ServiceRegistry registry;
	ow::adapters::PostgreSQLAdapter persistence;
	ow::economy::EconomySystem economy(registry, &persistence);
	ow::core::ModuleManager modules;
	if (!modules.RegisterModule(economy) || registry.Get<ow::economy::IEconomyService>() || !modules.StartAll())
	{
		return false;
	}

	auto* service = registry.Get<ow::economy::IEconomyService>();
	if (!service || registry.Get<ow::economy::IEconomyService>(2) || service->LoadWallet("").has_value() ||
		service->GetLastError().empty() || service->ApplyTransaction("", "", 0, "").has_value())
	{
		return false;
	}
	modules.StopAll();
	return registry.Get<ow::economy::IEconomyService>() == nullptr;
}

bool TestInventoryModuleServiceLifecycle()
{
	ow::core::ServiceRegistry registry;
	ow::adapters::PostgreSQLAdapter persistence;
	ow::inventory::InventorySystem inventory(registry, &persistence);
	ow::core::ModuleManager modules;
	if (!modules.RegisterModule(inventory) || registry.Get<ow::inventory::IInventoryService>() || !modules.StartAll())
	{
		return false;
	}

	auto* service = registry.Get<ow::inventory::IInventoryService>();
	std::uint64_t revision {};
	if (!service || registry.Get<ow::inventory::IInventoryService>(2) || service->Load("").has_value() ||
		service->GetLastError().empty() || service->Save("", 0, "[]", revision))
	{
		return false;
	}
	modules.StopAll();
	return registry.Get<ow::inventory::IInventoryService>() == nullptr;
}

bool TestTransferStateMachine()
{
	using namespace ow::transfer;
	const auto now = std::chrono::system_clock::now();
	TransferManager manager;
	auto transfer = MakeTransfer("transfer-1", "request-1", now);
	if (manager.Request(transfer, false, now) || !manager.Request(transfer, true, now) ||
		!manager.Request(transfer, true, now))
	{
		return false;
	}
	if (manager.Request(MakeTransfer("transfer-2", "request-2", now), true, now))
	{
		return false;
	}

	const std::vector<TransferState> transitions{
		TransferState::Validating, TransferState::DestinationReserved, TransferState::StateSaved,
		TransferState::Redirecting, TransferState::Arriving, TransferState::Completed};
	for (const TransferState state : transitions)
	{
		if (!manager.Advance("transfer-1", state, now))
		{
			return false;
		}
	}
	const auto completed = manager.Find("transfer-1");
	return completed && completed->state == TransferState::Completed &&
		   !manager.Advance("transfer-1", TransferState::Requested, now) &&
		   manager.GetPendingForPlayer(completed->snapshot.steamId).empty();
}

bool TestPlayerRouteWorker()
{
	ow::persistence::PlayerRouteWorker worker(
		[](const ow::persistence::PlayerRouteJob& job, ow::persistence::PlayerRouteResult& result)
		{
			result.serverId = job.operation == ow::persistence::PlayerRouteOperation::Resolve
				? job.lobbyServerId
				: job.destinationServerId;
		});
	constexpr std::uint64_t steamId = 76561198000000000ull;
	if (!worker.RequestResolve(1, steamId, "production", "west", "lobby") ||
		worker.RequestResolve(2, 0, "production", "west", "lobby") ||
		worker.RequestResolve(3, steamId, "production", "", "lobby"))
	{
		return false;
	}

	auto takeResult = [&worker]() -> std::optional<ow::persistence::PlayerRouteResult> {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (auto result = worker.TryTakeResult())
			{
				return result;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return std::nullopt;
	};

	const auto resolved = takeResult();
	if (!resolved || resolved->requestId != 1 || resolved->steamId != steamId ||
		resolved->operation != ow::persistence::PlayerRouteOperation::Resolve ||
		resolved->serverId != "lobby" || !resolved->error.empty())
	{
		return false;
	}
	if (!worker.RequestAssign(4, steamId, "production", "west"))
	{
		return false;
	}
	const auto assigned = takeResult();
	return assigned && assigned->requestId == 4 && assigned->steamId == steamId &&
		   assigned->operation == ow::persistence::PlayerRouteOperation::Assign &&
		   assigned->serverId == "west" && assigned->error.empty() &&
		   !worker.RequestAssign(5, steamId, "production", std::string_view("bad\0server", 10));
}

bool TestRedirectEndpointValidation()
{
	using ow::adapters::ClientRedirector;
	return ClientRedirector::IsValidEndpoint("127.0.0.1:27015") && ClientRedirector::IsValidEndpoint("server.example.net:65535")
		   && ClientRedirector::IsValidEndpoint("[2001:db8::1]:27015") && ClientRedirector::IsValidEndpoint("[::1]:27015")
		   && !ClientRedirector::IsValidEndpoint("server.example.net:0") && !ClientRedirector::IsValidEndpoint("server.example.net:65536")
		   && !ClientRedirector::IsValidEndpoint("server.example.net;quit:27015") && !ClientRedirector::IsValidEndpoint("server..example.net:27015")
		   && !ClientRedirector::IsValidEndpoint("[not-ipv6]:27015") && !ClientRedirector::IsValidEndpoint("[::::]:27015")
		   && !ClientRedirector::IsValidEndpoint("[1:2:3:4:5:6:7:8:]:27015") && !ClientRedirector::IsValidEndpoint("[:1:2:3:4:5:6:7:8]:27015")
		   && !ClientRedirector::IsValidEndpoint("[1:2:3:4:5:6:7:8:9]:27015");
}
} // namespace

int main()
{
	if (!TestModuleManager())
	{
		std::fprintf(stderr, "Module manager tests failed.\n");
		return 1;
	}
	if (!TestEventBus())
	{
		std::fprintf(stderr, "Event bus tests failed.\n");
		return 1;
	}
	if (!TestServiceRegistry())
	{
		std::fprintf(stderr, "Service registry tests failed.\n");
		return 1;
	}
	if (!TestClusterConfig())
	{
		std::fprintf(stderr, "Cluster configuration tests failed.\n");
		return 1;
	}
	if (!TestConfigExamples())
	{
		std::fprintf(stderr, "Configuration examples failed to load.\n");
		return 1;
	}
#ifdef OW_ENABLE_CACHE_FALLBACK
	if (!TestMemoryCache())
	{
		std::fprintf(stderr, "Memory cache tests failed.\n");
		return 1;
	}
	if (!TestPlayerWarmup())
	{
		std::fprintf(stderr, "Player warmup tests failed.\n");
		return 1;
	}
	if (!TestRedisCacheFailures())
	{
		std::fprintf(stderr, "Redis cache failure handling tests failed.\n");
		return 1;
	}
	if (!TestRedisCacheIntegrationWhenConfigured())
	{
		std::fprintf(stderr, "Redis cache integration test failed.\n");
		return 1;
	}
#endif
	if (!TestPostgreSQLAdapterInputValidation())
	{
		std::fprintf(stderr, "PostgreSQL adapter input validation tests failed.\n");
		return 1;
	}
	if (!TestPlayerSettingsCache())
	{
		std::fprintf(stderr, "Player settings cache tests failed.\n");
		return 1;
	}
	if (!TestPlayerSettingsWorker())
	{
		std::fprintf(stderr, "Player settings worker tests failed.\n");
		return 1;
	}
	if (!TestPlayerCharacterWorker())
	{
		std::fprintf(stderr, "Player character worker tests failed.\n");
		return 1;
	}
	if (!TestPostgreSQLIntegrationWhenConfigured())
	{
		std::fprintf(stderr, "PostgreSQL integration test failed.\n");
		return 1;
	}
	if (!TestTransferModuleServiceLifecycle())
	{
		std::fprintf(stderr, "Transfer module service lifecycle tests failed.\n");
		return 1;
	}
	if (!TestInventoryModuleServiceLifecycle())
	{
		std::fprintf(stderr, "Inventory module service lifecycle tests failed.\n");
		return 1;
	}
	if (!TestEconomyModuleServiceLifecycle())
	{
		std::fprintf(stderr, "Economy module service lifecycle tests failed.\n");
		return 1;
	}
	if (!TestTransferStateMachine())
	{
		std::fprintf(stderr, "Transfer state machine tests failed.\n");
		return 1;
	}
	if (!TestPlayerRouteWorker())
	{
		std::fprintf(stderr, "Player route worker tests failed.\n");
		return 1;
	}
	if (!TestRedirectEndpointValidation())
	{
		std::fprintf(stderr, "Redirect endpoint validation tests failed.\n");
		return 1;
	}
	return 0;
}
