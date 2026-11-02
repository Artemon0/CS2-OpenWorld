#include "inventory_system.h"

namespace ow::inventory
{
InventorySystem::InventorySystem(ow::core::ServiceRegistry& serviceRegistryRef,
								 ow::adapters::PostgreSQLAdapter* persistenceRef)
	: serviceRegistry(serviceRegistryRef), persistence(persistenceRef)
{
}

bool InventorySystem::Initialize()
{
	return persistence != nullptr;
}

bool InventorySystem::Start()
{
	if (serviceRegistered)
	{
		return true;
	}
	serviceRegistered = serviceRegistry.Register<IInventoryService>(*this, 1);
	return serviceRegistered;
}

void InventorySystem::Stop()
{
	if (serviceRegistered)
	{
		serviceRegistry.Unregister<IInventoryService>(*this);
		serviceRegistered = false;
	}
}

std::optional<InventorySnapshot> InventorySystem::Load(std::string_view characterId)
{
	if (!persistence)
	{
		return std::nullopt;
	}
	const auto inventory = persistence->LoadInventory(characterId);
	if (!inventory)
	{
		return std::nullopt;
	}
	return InventorySnapshot{inventory->revision, inventory->itemsJson};
}

bool InventorySystem::Save(std::string_view characterId, std::uint64_t expectedRevision,
					   std::string_view itemsJson, std::uint64_t& newRevision)
{
	return persistence && persistence->SaveInventory(characterId, expectedRevision, itemsJson, newRevision);
}

std::string InventorySystem::GetLastError() const
{
	return persistence ? persistence->GetLastError() : "PostgreSQL adapter is unavailable.";
}
} // namespace ow::inventory
