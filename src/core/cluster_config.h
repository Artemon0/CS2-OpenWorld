#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ow::core
{
struct ServerEndpoint
{
	std::string id;
	std::string connect;
	std::string map;
	std::string workshopCollectionId;
	int maxPlayers {};
};

struct ZoneOwnership
{
	std::string id;
	std::string serverId;
	std::string map;
	double minimumX {};
	double minimumY {};
	double maximumX {};
	double maximumY {};
};

enum class WorldMode
{
	Cluster,
	SingleMap
};

struct AdapterSettings
{
	bool postgresql {true};
	bool panorama {true};
	bool hud {true};
	bool input {true};
	bool mapOverview {true};
	bool particleMenu {true};
};

struct ModuleSettings
{
	bool characters {true};
	bool economy {true};
	bool inventory {true};
	bool transfer {true};
};

struct RoutingSettings
{
	bool enforceServerAffinity {};
	std::string lobbyServerId;
};

class ClusterConfig
{
public:
	static std::optional<ClusterConfig> Load(const std::string& path, std::string* error = nullptr);
	static std::optional<ClusterConfig> Parse(std::string_view contents, std::string* error = nullptr);

	const ServerEndpoint* CurrentServer() const;
	const ServerEndpoint* FindServer(std::string_view id) const;
	const ZoneOwnership* FindZone(std::string_view map, double x, double y) const;
	const ZoneOwnership* FindTransitionZone(std::string_view map, std::string_view currentServerId, double x,
										   double y) const;

	std::string serverId;
	std::string environment;
	bool enabled {true};
	WorldMode worldMode {WorldMode::Cluster};
	std::string worldMap;
	AdapterSettings adapters;
	ModuleSettings modules;
	RoutingSettings routing;
	std::string persistenceProvider;
	std::string persistenceConnectionEnv;
	std::string cacheProvider;
	std::string sharedCacheProvider;
	std::size_t cacheWarmupWorkers {2};
	std::size_t cacheWarmupQueueCapacity {4096};
	std::string redisHost;
	std::uint16_t redisPort {6379};
	int redisDatabase {};
	std::string redisUsernameEnv;
	std::string redisPasswordEnv;
	std::string redisKeyPrefix;
	int redisTimeoutMs {1000};
	std::size_t redisMaxValueBytes {4 * 1024 * 1024};
	std::vector<ServerEndpoint> servers;
	std::vector<ZoneOwnership> zones;
};
} // namespace ow::core
