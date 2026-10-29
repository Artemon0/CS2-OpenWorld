#include "core/event_bus.h"
#include "core/cluster_config.h"
#include "core/module_manager.h"
#include "core/service_registry.h"
#include "adapters/cs2/client_redirector.h"
#include "adapters/persistence/postgres_adapter.h"
#include "adapters/persistence/player_settings_cache.h"
#include "modules/persistence/player_settings_worker.h"
#ifdef OW_ENABLE_CACHE_FALLBACK
#include "modules/persistence/cache_manager.h"
#include "modules/persistence/player_warmup.h"
#include "modules/persistence/redis_cache.h"
#endif
#include "modules/transfer/transfer_manager.h"

#include <chrono>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
	TestModule dependent{"dependent", {"base"}, &calls};
	TestModule base{"base", {}, &calls};
	ow::core::ModuleManager manager;
	if (!manager.RegisterModule(dependent) || !manager.RegisterModule(base) || !manager.StartAll() ||
		!manager.IsRunning())
	{
		return false;
	}

	const std::vector<std::string> expected{"init:base", "start:base", "init:dependent", "start:dependent"};
	if (calls != expected)
	{
		return false;
	}

	manager.StopAll();
	if (calls.size() != 6 || calls[4] != "stop:dependent" || calls[5] != "stop:base")
	{
		return false;
	}

	std::vector<std::string> failedCalls;
	TestModule failing{"failing", {}, &failedCalls};
	failing.startSucceeds = false;
	ow::core::ModuleManager failingManager;
	return failingManager.RegisterModule(failing) && !failingManager.StartAll() && !failingManager.IsRunning() &&
		   failingManager.GetLastError() == "Module start failed: failing" && failedCalls.size() == 3 &&
		   failedCalls.back() == "stop:failing";
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
	if (!stalePlan.AddRequired("player:42:stale-critical", 4, std::chrono::seconds(30), [] {
			return std::optional<CacheLoadValue>{{"outdated", 3}};
		}) ||
		!stalePlan.Start(staleWarmer))
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
[servers.a]
connect = 127.0.0.1:27015
map = map_a
max_players = 32
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
map = map_a
min_x = 0
min_y = -100
max_x = 100
max_y = 100
)";
	std::string error;
	auto config = ow::core::ClusterConfig::Parse(validConfig, &error);
	if (!config || !error.empty() || !config->CurrentServer() ||
		!config->FindServer("b") || config->FindServer("b")->connect != "server.example.net:27016" ||
		config->FindServer("missing"))
	{
		return false;
	}
	const auto* westZone = config->FindZone("map_a", -50, 0);
	const auto* eastZone = config->FindZone("map_a", 0, 0);
	if (!westZone || !eastZone || westZone->serverId != "a" || eastZone->serverId != "b" ||
		config->FindZone("map_a", 100, 0) != nullptr)
	{
		return false;
	}
	if (config->redisHost != "127.0.0.1" || config->redisPort != 6379 || config->redisDatabase != 1 ||
		config->redisKeyPrefix != "openworld:test:" || config->redisPasswordEnv != "OW_REDIS_PASSWORD")
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

	std::string tlsConfig = validConfig;
	const std::string disabledTls = "redis_tls = false";
	const std::size_t tlsPosition = tlsConfig.find(disabledTls);
	tlsConfig.replace(tlsPosition, disabledTls.size(), "redis_tls = true");
	return !ow::core::ClusterConfig::Parse(tlsConfig, &error) && error.find("TLS") != std::string::npos;
}

#ifdef OW_ENABLE_CACHE_FALLBACK
bool TestRedisCacheFailures()
{
	ow::persistence::RedisCacheConfig config;
	config.host = "127.0.0.1";
	config.port = 1;
	config.timeout = std::chrono::milliseconds(100);
	ow::persistence::RedisCache cache(std::move(config));
	return !cache.Ping() && !cache.GetLastError().empty() &&
		   !cache.Put("key", "value", 1, std::chrono::seconds(1));
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
	if (converted.ec != std::errc {} || converted.ptr != portValue.data() + portValue.size() || port == 0 ||
		port > 65535)
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

	const std::string idempotencyKey = "ow-core-tests:" + *characterId;
	const auto credited = adapter.ApplyMoneyTransaction(*characterId, idempotencyKey, 25, "Integration test");
	const auto duplicate = adapter.ApplyMoneyTransaction(*characterId, idempotencyKey, 25, "Integration test");
	return credited && duplicate && credited->balance == 25 && !credited->duplicate && duplicate->balance == 25 &&
		   duplicate->duplicate;
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

bool TestRedirectEndpointValidation()
{
	using ow::adapters::ClientRedirector;
	return ClientRedirector::IsValidEndpoint("127.0.0.1:27015") &&
		   ClientRedirector::IsValidEndpoint("server.example.net:65535") &&
		   ClientRedirector::IsValidEndpoint("[2001:db8::1]:27015") &&
		   ClientRedirector::IsValidEndpoint("[::1]:27015") &&
		   !ClientRedirector::IsValidEndpoint("server.example.net:0") &&
		   !ClientRedirector::IsValidEndpoint("server.example.net:65536") &&
		   !ClientRedirector::IsValidEndpoint("server.example.net;quit:27015") &&
		   !ClientRedirector::IsValidEndpoint("server..example.net:27015") &&
		   !ClientRedirector::IsValidEndpoint("[not-ipv6]:27015") &&
		   !ClientRedirector::IsValidEndpoint("[::::]:27015") &&
		   !ClientRedirector::IsValidEndpoint("[1:2:3:4:5:6:7:8:]:27015") &&
		   !ClientRedirector::IsValidEndpoint("[:1:2:3:4:5:6:7:8]:27015") &&
		   !ClientRedirector::IsValidEndpoint("[1:2:3:4:5:6:7:8:9]:27015");
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
	if (!TestClusterConfig())
	{
		std::fprintf(stderr, "Cluster configuration tests failed.\n");
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
	if (!TestPostgreSQLIntegrationWhenConfigured())
	{
		std::fprintf(stderr, "PostgreSQL integration test failed.\n");
		return 1;
	}
	if (!TestTransferStateMachine())
	{
		std::fprintf(stderr, "Transfer state machine tests failed.\n");
		return 1;
	}
	if (!TestRedirectEndpointValidation())
	{
		std::fprintf(stderr, "Redirect endpoint validation tests failed.\n");
		return 1;
	}
	return 0;
}
