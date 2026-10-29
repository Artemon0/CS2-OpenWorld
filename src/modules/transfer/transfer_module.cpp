#include "transfer_module.h"

namespace ow::modules
{
TransferModule::TransferModule(ow::core::ServiceRegistry& serviceRegistryRef, ow::core::EventBus& eventBusRef)
	: serviceRegistry(serviceRegistryRef), eventBus(eventBusRef)
{
}

bool TransferModule::Initialize()
{
	return true;
}

bool TransferModule::Start()
{
	return true;
}

void TransferModule::Stop()
{
}
} // namespace ow::modules
