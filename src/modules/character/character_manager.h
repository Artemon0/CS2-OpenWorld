#pragma once

#include "core/module.h"
#include "core/service_registry.h"
#include "core/event_bus.h"
#include "adapters/persistence/postgres_adapter.h"

namespace ow::modules
{
class CharacterManager : public ow::core::IModule
{
public:
	CharacterManager(ow::core::ServiceRegistry& serviceRegistry, ow::core::EventBus& eventBus,
					 ow::adapters::PostgreSQLAdapter* persistence);

	std::string GetId() const override { return "character_manager"; }
	std::vector<std::string> GetDependencies() const override { return {}; }
	bool Initialize() override;
	bool Start() override;
	void Stop() override;

private:
	ow::core::ServiceRegistry& serviceRegistry;
	ow::core::EventBus& eventBus;
	ow::adapters::PostgreSQLAdapter* persistence;
};
} // namespace ow::modules
