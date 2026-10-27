#pragma once

#include <cstdint>
#include <typeindex>
#include <unordered_map>

namespace ow::core
{
class ServiceRegistry
{
public:
	template <typename Interface>
	bool Register(Interface& service, std::uint32_t version)
	{
		const auto key = std::type_index(typeid(Interface));
		if (version == 0 || services.find(key) != services.end())
		{
			return false;
		}
		services.emplace(key, Entry{&service, version});
		return true;
	}

	template <typename Interface>
	Interface* Get(std::uint32_t minimumVersion = 1) const
	{
		const auto found = services.find(std::type_index(typeid(Interface)));
		if (found == services.end() || found->second.version < minimumVersion)
		{
			return nullptr;
		}
		return static_cast<Interface*>(found->second.instance);
	}

	template <typename Interface>
	bool Unregister(Interface& service)
	{
		const auto found = services.find(std::type_index(typeid(Interface)));
		if (found == services.end() || found->second.instance != &service)
		{
			return false;
		}
		services.erase(found);
		return true;
	}

private:
	struct Entry
	{
		void* instance;
		std::uint32_t version;
	};

	std::unordered_map<std::type_index, Entry> services;
};
} // namespace ow::core
