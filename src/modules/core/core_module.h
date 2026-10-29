#pragma once

#include "core/module.h"
#include "core/service_registry.h"
#include "core/event_bus.h"

namespace ow::modules
{
class CoreModule : public ow::core::IModule
{
public:
	CoreModule(ow::core::ServiceRegistry& serviceRegistry, ow::core::EventBus& eventBus);

	std::string GetId() const override { return "core"; }
	std::vector<std::string> GetDependencies() const override { return {}; }
	bool Initialize() override;
	bool Start() override;
	void Stop() override;

private:
	ow::core::ServiceRegistry& serviceRegistry;
	ow::core::EventBus& eventBus;
};
} // namespace ow::modules
