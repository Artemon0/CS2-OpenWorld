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
	if (serviceRegistered)
	{
		return true;
	}
	serviceRegistered = serviceRegistry.Register<ow::transfer::ITransferService>(transferManager, 1);
	return serviceRegistered;
}

void TransferModule::Stop()
{
	if (serviceRegistered)
	{
		serviceRegistry.Unregister<ow::transfer::ITransferService>(transferManager);
		serviceRegistered = false;
	}
}
} // namespace ow::modules
