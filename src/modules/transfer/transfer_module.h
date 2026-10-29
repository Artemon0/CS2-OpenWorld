#pragma once

#include "core/module.h"
#include "core/service_registry.h"
#include "core/event_bus.h"
#include "modules/transfer/transfer_manager.h"

namespace ow::modules
{
class TransferModule : public ow::core::IModule
{
public:
	TransferModule(ow::core::ServiceRegistry& serviceRegistry, ow::core::EventBus& eventBus);

	std::string GetId() const override { return "transfer"; }
	std::vector<std::string> GetDependencies() const override { return {}; }
	bool Initialize() override;
	bool Start() override;
	void Stop() override;

	ow::transfer::TransferManager& GetTransferManager() { return transferManager; }

private:
	ow::core::ServiceRegistry& serviceRegistry;
	ow::core::EventBus& eventBus;
	ow::transfer::TransferManager transferManager;
};
} // namespace ow::modules
