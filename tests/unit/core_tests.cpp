#include "core/event_bus.h"
#include "core/module_manager.h"
#include "core/service_registry.h"
#include "adapters/cs2/client_redirector.h"
#include "modules/persistence/cache_manager.h"
#include "modules/transfer/transfer_manager.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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
	TestModule(std::string moduleId, std::vector<std::string_view> moduleDependencies, std::vector<std::string>* callLog)
		: id(std::move(moduleId)), dependencies(std::move(moduleDependencies)), calls(callLog)
	{
	}

	std::string id;
	std::vector<std::string_view> dependencies;
	std::vector<std::string>* calls {};
	bool initializeSucceeds {true};
	bool startSucceeds {true};

	std::string_view GetId() const override { return id; }
	std::vector<std::string_view> GetDependencies() const override { return dependencies; }
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

	return cache.Put("expired:entry", "old", 10, std::chrono::seconds(-1)) == false;
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
	if (!TestMemoryCache())
	{
		std::fprintf(stderr, "Memory cache tests failed.\n");
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
