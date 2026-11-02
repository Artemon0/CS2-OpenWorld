#pragma once

#include "adapters/persistence/postgres_adapter.h"
#include "core/module.h"
#include "core/service_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ow::inventory
{
struct InventorySnapshot
{
	std::uint64_t revision {};
	std::string itemsJson;
};

class IInventoryService
{
public:
	virtual ~IInventoryService() = default;
	virtual std::optional<InventorySnapshot> Load(std::string_view characterId) = 0;
	virtual bool Save(std::string_view characterId, std::uint64_t expectedRevision, std::string_view itemsJson,
					  std::uint64_t& newRevision) = 0;
	virtual std::string GetLastError() const = 0;
};

class InventorySystem final : public ow::core::IModule, public IInventoryService
{
public:
	InventorySystem(ow::core::ServiceRegistry& serviceRegistry, ow::adapters::PostgreSQLAdapter* persistence);

	std::string GetId() const override { return "inventory"; }
	bool Initialize() override;
	bool Start() override;
	void Stop() override;

	std::optional<InventorySnapshot> Load(std::string_view characterId) override;
	bool Save(std::string_view characterId, std::uint64_t expectedRevision, std::string_view itemsJson,
			  std::uint64_t& newRevision) override;
	std::string GetLastError() const override;

private:
	ow::core::ServiceRegistry& serviceRegistry;
	ow::adapters::PostgreSQLAdapter* persistence {};
	bool serviceRegistered {};
};
} // namespace ow::inventory
