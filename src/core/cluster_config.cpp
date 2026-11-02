#include "cluster_config.h"

#include "adapters/cs2/client_redirector.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace ow::core
{
namespace
{
using Section = std::map<std::string, std::string>;

std::string Trim(std::string_view value)
{
	const std::size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
	{
		return {};
	}
	const std::size_t last = value.find_last_not_of(" \t\r\n");
	return std::string(value.substr(first, last - first + 1));
}

bool ParseBoolean(const Section& section, const char* key, bool defaultValue, bool& result)
{
	const auto value = section.find(key);
	if (value == section.end())
	{
		result = defaultValue;
		return true;
	}
	if (value->second == "true" || value->second == "1")
	{
		result = true;
		return true;
	}
	if (value->second == "false" || value->second == "0")
	{
		result = false;
		return true;
	}
	return false;
}

bool ParseInteger(const Section& section, const char* key, int defaultValue, int& result)
{
	const auto value = section.find(key);
	if (value == section.end())
	{
		result = defaultValue;
		return true;
	}
	int parsed {};
	const auto converted = std::from_chars(value->second.data(), value->second.data() + value->second.size(), parsed);
	if (converted.ec != std::errc {} || converted.ptr != value->second.data() + value->second.size())
	{
		return false;
	}
	result = parsed;
	return true;
}

bool ParseSize(const Section& section, const char* key, std::size_t defaultValue, std::size_t& result)
{
	const auto value = section.find(key);
	if (value == section.end())
	{
		result = defaultValue;
		return true;
	}
	if (value->second.empty() || value->second.front() == '-')
	{
		return false;
	}
	unsigned long long parsed {};
	const auto converted = std::from_chars(value->second.data(), value->second.data() + value->second.size(), parsed);
	if (converted.ec != std::errc {} || converted.ptr != value->second.data() + value->second.size() ||
		parsed > (std::numeric_limits<std::size_t>::max)())
	{
		return false;
	}
	result = static_cast<std::size_t>(parsed);
	return true;
}

bool IsSafeMapName(std::string_view mapName)
{
	return !mapName.empty() && mapName.size() <= 64 &&
		   std::all_of(mapName.begin(), mapName.end(), [](unsigned char character) {
			   return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
					  (character >= '0' && character <= '9') || character == '_' || character == '-';
		   });
}

bool ParseDouble(const Section& section, const char* key, double& result)
{
	const auto value = section.find(key);
	if (value == section.end())
	{
		return false;
	}
	double parsed {};
	const auto converted = std::from_chars(value->second.data(), value->second.data() + value->second.size(), parsed,
										   std::chars_format::general);
	if (converted.ec != std::errc {} || converted.ptr != value->second.data() + value->second.size() ||
		!std::isfinite(parsed))
	{
		return false;
	}
	result = parsed;
	return true;
}

std::string Get(const Section& section, const char* key, std::string defaultValue = {})
{
	const auto value = section.find(key);
	return value == section.end() ? std::move(defaultValue) : value->second;
}

bool HasOnlyKeys(const Section& section, std::initializer_list<std::string_view> allowed)
{
	for (const auto& entry : section)
	{
		bool found = false;
		for (const std::string_view key : allowed)
		{
			if (entry.first == key)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return false;
		}
	}
	return true;
}

bool SetError(std::string* error, std::string message)
{
	if (error)
	{
		*error = std::move(message);
	}
	return false;
}

bool ParseSections(std::string_view contents, std::map<std::string, Section>& sections, std::string* error)
{
	if (contents.starts_with("\xEF\xBB\xBF"))
	{
		contents.remove_prefix(3);
	}
	std::istringstream input{std::string(contents)};
	std::string line;
	std::string currentSection;
	std::size_t lineNumber = 0;
	while (std::getline(input, line))
	{
		++lineNumber;
		std::string cleaned = Trim(line);
		if (cleaned.empty() || cleaned.front() == '#' || cleaned.front() == ';')
		{
			continue;
		}
		if (cleaned.front() == '[')
		{
			if (cleaned.size() < 3 || cleaned.back() != ']')
			{
				return SetError(error, "Malformed section header on line " + std::to_string(lineNumber) + ".");
			}
			currentSection = Trim(std::string_view(cleaned).substr(1, cleaned.size() - 2));
			if (currentSection.empty() || !sections.emplace(currentSection, Section{}).second)
			{
				return SetError(error, "Empty or duplicate section on line " + std::to_string(lineNumber) + ".");
			}
			continue;
		}
		const std::size_t separator = cleaned.find('=');
		if (currentSection.empty() || separator == std::string::npos)
		{
			return SetError(error, "Expected key=value inside a section on line " + std::to_string(lineNumber) + ".");
		}
		std::string key = Trim(std::string_view(cleaned).substr(0, separator));
		std::string value = Trim(std::string_view(cleaned).substr(separator + 1));
		if (key.empty())
		{
			return SetError(error, "Empty key on line " + std::to_string(lineNumber) + ".");
		}
		if (!value.empty() && (value.front() == '"' || value.back() == '"'))
		{
			if (value.size() < 2 || value.front() != '"' || value.back() != '"')
			{
				return SetError(error, "Malformed quoted value on line " + std::to_string(lineNumber) + ".");
			}
			value = value.substr(1, value.size() - 2);
		}
		if (value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos ||
			!sections[currentSection].emplace(std::move(key), std::move(value)).second)
		{
			return SetError(error, "Invalid or duplicate setting on line " + std::to_string(lineNumber) + ".");
		}
	}
	return true;
}
} // namespace

std::optional<ClusterConfig> ClusterConfig::Load(const std::string& path, std::string* error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		SetError(error, "Could not open cluster configuration: " + path);
		return std::nullopt;
	}
	std::ostringstream contents;
	contents << file.rdbuf();
	if (!file.good() && !file.eof())
	{
		SetError(error, "Could not read cluster configuration: " + path);
		return std::nullopt;
	}
	return Parse(contents.str(), error);
}

std::optional<ClusterConfig> ClusterConfig::Parse(std::string_view contents, std::string* error)
{
	std::map<std::string, Section> sections;
	if (!ParseSections(contents, sections, error))
	{
		return std::nullopt;
	}
	const auto core = sections.find("core");
	const auto persistence = sections.find("persistence");
	const auto cache = sections.find("cache");
	const auto world = sections.find("world");
	const auto adapters = sections.find("adapters");
	const auto modules = sections.find("modules");
	const auto routing = sections.find("routing");
	if (core == sections.end() || persistence == sections.end() || cache == sections.end())
	{
		SetError(error, "Configuration must define [core], [persistence], and [cache] sections.");
		return std::nullopt;
	}

	const Section emptySection;
	const Section& worldSettings = world == sections.end() ? emptySection : world->second;
	const Section& adapterSettings = adapters == sections.end() ? emptySection : adapters->second;
	const Section& moduleSettings = modules == sections.end() ? emptySection : modules->second;
	const Section& routingSettings = routing == sections.end() ? emptySection : routing->second;
	if (!HasOnlyKeys(core->second, {"server_id", "environment", "enabled"}) || !HasOnlyKeys(persistence->second, {"provider", "connection_env"})
		|| !HasOnlyKeys(cache->second,
						{"provider", "shared_provider", "redis_host", "redis_port", "redis_database", "redis_username_env", "redis_password_env",
						 "redis_key_prefix", "redis_timeout_ms", "redis_max_value_bytes", "redis_tls", "warmup_workers", "warmup_queue_capacity"})
		|| !HasOnlyKeys(worldSettings, {"mode", "map"})
		|| !HasOnlyKeys(adapterSettings, {"postgresql", "panorama", "hud", "input", "map_overview", "particle_menu"})
		|| !HasOnlyKeys(routingSettings, {"enforce_server_affinity", "lobby_server_id"})
		|| !HasOnlyKeys(moduleSettings, {"characters", "economy", "inventory", "transfer"}))
	{
		SetError(error, "Configuration contains an unknown core, world, adapter, module, persistence, or cache setting.");
		return std::nullopt;
	}

	ClusterConfig config;
	config.serverId = Get(core->second, "server_id");
	config.environment = Get(core->second, "environment");
	if (!ParseBoolean(core->second, "enabled", true, config.enabled))
	{
		SetError(error, "core.enabled must be true, false, 1, or 0.");
		return std::nullopt;
	}
	const std::string worldMode = Get(worldSettings, "mode", "cluster");
	if (worldMode == "cluster")
	{
		config.worldMode = WorldMode::Cluster;
		if (worldSettings.find("map") != worldSettings.end())
		{
			SetError(error, "world.map is only valid when world.mode is single_map.");
			return std::nullopt;
		}
	}
	else if (worldMode == "single_map")
	{
		config.worldMode = WorldMode::SingleMap;
		config.worldMap = Get(worldSettings, "map");
		if (!IsSafeMapName(config.worldMap))
		{
			SetError(error, "world.map must be a simple map name containing only letters, digits, '_' or '-'.");
			return std::nullopt;
		}
	}
	else
	{
		SetError(error, "world.mode must be 'cluster' or 'single_map'.");
		return std::nullopt;
	}
	if (!ParseBoolean(adapterSettings, "postgresql", true, config.adapters.postgresql)
		|| !ParseBoolean(adapterSettings, "panorama", true, config.adapters.panorama)
		|| !ParseBoolean(adapterSettings, "hud", true, config.adapters.hud) || !ParseBoolean(adapterSettings, "input", true, config.adapters.input)
		|| !ParseBoolean(adapterSettings, "map_overview", true, config.adapters.mapOverview)
		|| !ParseBoolean(adapterSettings, "particle_menu", true, config.adapters.particleMenu))
	{
		SetError(error, "Adapter settings must be true, false, 1, or 0.");
		return std::nullopt;
	}
	if (!ParseBoolean(moduleSettings, "characters", config.adapters.postgresql, config.modules.characters)
		|| !ParseBoolean(moduleSettings, "economy", config.adapters.postgresql, config.modules.economy)
		|| !ParseBoolean(moduleSettings, "inventory", config.adapters.postgresql, config.modules.inventory)
		|| !ParseBoolean(moduleSettings, "transfer", config.worldMode == WorldMode::Cluster, config.modules.transfer))
	{
		SetError(error, "Module settings must be true, false, 1, or 0.");
		return std::nullopt;
	}
	if (!ParseBoolean(routingSettings, "enforce_server_affinity", false, config.routing.enforceServerAffinity))
	{
		SetError(error, "routing.enforce_server_affinity must be true, false, 1, or 0.");
		return std::nullopt;
	}
	config.routing.lobbyServerId = Get(routingSettings, "lobby_server_id");
	config.persistenceProvider = Get(persistence->second, "provider");
	config.persistenceConnectionEnv = Get(persistence->second, "connection_env");
	config.cacheProvider = Get(cache->second, "provider");
	config.sharedCacheProvider = Get(cache->second, "shared_provider", "none");
	config.redisHost = Get(cache->second, "redis_host", "127.0.0.1");
	config.redisUsernameEnv = Get(cache->second, "redis_username_env");
	config.redisPasswordEnv = Get(cache->second, "redis_password_env");
	config.redisKeyPrefix = Get(cache->second, "redis_key_prefix", "openworld:development:");

	int redisPort {};
	if (!ParseInteger(cache->second, "redis_port", 6379, redisPort) || redisPort < 1 || redisPort > 65535
		|| !ParseInteger(cache->second, "redis_database", 0, config.redisDatabase) || config.redisDatabase < 0
		|| !ParseInteger(cache->second, "redis_timeout_ms", 1000, config.redisTimeoutMs) || config.redisTimeoutMs < 1
		|| !ParseSize(cache->second, "redis_max_value_bytes", 4 * 1024 * 1024, config.redisMaxValueBytes) || config.redisMaxValueBytes == 0
		|| !ParseSize(cache->second, "warmup_workers", 2, config.cacheWarmupWorkers) || config.cacheWarmupWorkers == 0
		|| config.cacheWarmupWorkers > 16 || !ParseSize(cache->second, "warmup_queue_capacity", 4096, config.cacheWarmupQueueCapacity)
		|| config.cacheWarmupQueueCapacity == 0 || config.cacheWarmupQueueCapacity > 65536)
	{
		SetError(error, "Redis port, database, timeout, or maximum value size is invalid.");
		return std::nullopt;
	}
	config.redisPort = static_cast<std::uint16_t>(redisPort);
	bool redisTls = false;
	if (!ParseBoolean(cache->second, "redis_tls", false, redisTls) || redisTls)
	{
		SetError(error, "Redis TLS is not implemented; use a private network or a TLS tunnel.");
		return std::nullopt;
	}
	if (!config.adapters.postgresql && (config.modules.characters || config.modules.economy || config.modules.inventory))
	{
		SetError(error, "The characters, economy, and inventory modules require adapters.postgresql=true.");
		return std::nullopt;
	}
	if (config.routing.enforceServerAffinity &&
		(!config.adapters.postgresql || config.worldMode != WorldMode::Cluster || config.routing.lobbyServerId.empty()))
	{
		SetError(error, "Server affinity requires PostgreSQL, cluster mode, and routing.lobby_server_id.");
		return std::nullopt;
	}
	if (config.adapters.postgresql && (config.persistenceProvider != "postgresql" || config.persistenceConnectionEnv.empty()))
	{
		SetError(error, "PostgreSQL requires persistence.provider=postgresql and a non-empty connection_env.");
		return std::nullopt;
	}
	if (!config.adapters.postgresql && config.persistenceProvider != "none" && config.persistenceProvider != "postgresql")
	{
		SetError(error, "When PostgreSQL is disabled, persistence.provider must be 'none' or 'postgresql'.");
		return std::nullopt;
	}
	if (config.serverId.empty() || config.environment.empty() || (config.cacheProvider != "memory" && config.cacheProvider != "redis")
		|| (config.sharedCacheProvider != "none" && config.sharedCacheProvider != "redis")
		|| (config.cacheProvider == "redis" && config.sharedCacheProvider != "redis") || config.redisHost.empty() || config.redisKeyPrefix.empty()
		|| config.redisKeyPrefix.size() > 512 || config.redisKeyPrefix.back() != ':'
		|| config.redisKeyPrefix.rfind("openworld:" + config.environment + ":", 0) != 0 || config.redisMaxValueBytes > 15 * 1024 * 1024)
	{
		SetError(error, "Configuration has a missing or unsupported provider, identity, or namespace.");
		return std::nullopt;
	}
	for (const auto& section : sections)
	{
		const std::string& name = section.first;
		if (name == "core" || name == "persistence" || name == "cache" || name == "world" || name == "adapters" ||
			name == "modules" || name == "routing")
		{
			continue;
		}
		if (name.starts_with("servers."))
		{
			const std::string id = name.substr(8);
			ServerEndpoint server;
			server.id = id;
			server.connect = Get(section.second, "connect");
			server.map = Get(section.second, "map");
			server.workshopCollectionId = Get(section.second, "workshop_collection_id");
			if (id.empty() || server.connect.empty() || !ow::adapters::ClientRedirector::IsValidEndpoint(server.connect) || server.map.empty()
				|| !ParseInteger(section.second, "max_players", 64, server.maxPlayers) || server.maxPlayers < 1 || server.maxPlayers > 64
				|| !HasOnlyKeys(section.second, {"connect", "map", "workshop_collection_id", "max_players"}))
			{
				SetError(error, "Server section [" + name + "] contains invalid or unknown settings.");
				return std::nullopt;
			}
			config.servers.push_back(std::move(server));
			continue;
		}
		if (name.starts_with("zones."))
		{
			ZoneOwnership zone;
			zone.id = name.substr(6);
			zone.serverId = Get(section.second, "server_id");
			zone.map = Get(section.second, "map");
			if (zone.id.empty() || zone.serverId.empty() || zone.map.empty() || !ParseDouble(section.second, "min_x", zone.minimumX)
				|| !ParseDouble(section.second, "min_y", zone.minimumY) || !ParseDouble(section.second, "max_x", zone.maximumX)
				|| !ParseDouble(section.second, "max_y", zone.maximumY) || zone.minimumX >= zone.maximumX || zone.minimumY >= zone.maximumY
				|| !HasOnlyKeys(section.second, {"server_id", "map", "min_x", "min_y", "max_x", "max_y"}))
			{
				SetError(error, "Zone section [" + name + "] contains invalid or unknown settings.");
				return std::nullopt;
			}
			config.zones.push_back(std::move(zone));
			continue;
		}
		SetError(error, "Unknown configuration section [" + name + "].");
		return std::nullopt;
	}

	if (config.servers.empty() || !config.CurrentServer())
	{
		SetError(error, "Configuration must include this server.");
		return std::nullopt;
	}
	if (config.routing.enforceServerAffinity && !config.FindServer(config.routing.lobbyServerId))
	{
		SetError(error, "routing.lobby_server_id must reference a configured server.");
		return std::nullopt;
	}
	if (config.worldMode == WorldMode::SingleMap)
	{
		const ServerEndpoint* currentServer = config.CurrentServer();
		if (config.servers.size() != 1 || !config.zones.empty() || !currentServer || currentServer->map != config.worldMap)
		{
			SetError(error, "single_map mode requires exactly one server, no zones, and a matching server map.");
			return std::nullopt;
		}
	}
	else if (config.zones.empty())
	{
		SetError(error, "Cluster mode must include at least one world zone.");
		return std::nullopt;
	}
	for (std::size_t index = 0; index < config.zones.size(); ++index)
	{
		const ZoneOwnership& zone = config.zones[index];
		if (!config.FindServer(zone.serverId))
		{
			SetError(error, "Zone [" + zone.id + "] references unknown server [" + zone.serverId + "].");
			return std::nullopt;
		}
		for (std::size_t otherIndex = index + 1; otherIndex < config.zones.size(); ++otherIndex)
		{
			const ZoneOwnership& other = config.zones[otherIndex];
			const bool overlaps = zone.map == other.map && zone.minimumX < other.maximumX && zone.maximumX > other.minimumX
								  && zone.minimumY < other.maximumY && zone.maximumY > other.minimumY;
			if (overlaps)
			{
				SetError(error, "Zones [" + zone.id + "] and [" + other.id + "] overlap on map [" + zone.map + "].");
				return std::nullopt;
			}
		}
	}

	if (error)
	{
		error->clear();
	}
	return config;
}

const ServerEndpoint* ClusterConfig::CurrentServer() const
{
	return FindServer(serverId);
}

const ServerEndpoint* ClusterConfig::FindServer(std::string_view id) const
{
	for (const ServerEndpoint& server : servers)
	{
		if (server.id == id)
		{
			return &server;
		}
	}
	return nullptr;
}

const ZoneOwnership* ClusterConfig::FindZone(std::string_view map, double x, double y) const
{
	for (const ZoneOwnership& zone : zones)
	{
		if (zone.map == map && x >= zone.minimumX && x < zone.maximumX && y >= zone.minimumY &&
			y < zone.maximumY)
		{
			return &zone;
		}
	}
	return nullptr;
}

const ZoneOwnership* ClusterConfig::FindTransitionZone(std::string_view map, std::string_view currentServerId,
													   double x, double y) const
{
	bool currentServerHasMapZone = false;
	for (const ZoneOwnership& zone : zones)
	{
		if (zone.map != map || zone.serverId != currentServerId)
		{
			continue;
		}
		currentServerHasMapZone = true;
		if (x >= zone.minimumX && x < zone.maximumX && y >= zone.minimumY && y < zone.maximumY)
		{
			return nullptr;
		}
	}
	if (!currentServerHasMapZone)
	{
		return nullptr;
	}

	const ZoneOwnership* destination = nullptr;
	for (const ZoneOwnership& zone : zones)
	{
		if (zone.serverId == currentServerId || x < zone.minimumX || x >= zone.maximumX ||
			y < zone.minimumY || y >= zone.maximumY)
		{
			continue;
		}
		if (destination && destination->serverId != zone.serverId)
		{
			return nullptr;
		}
		destination = &zone;
	}
	return destination;
}

} // namespace ow::core
