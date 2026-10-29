#include "core_module.h"

namespace ow::modules
{
CoreModule::CoreModule(ow::core::ServiceRegistry& serviceRegistryRef, ow::core::EventBus& eventBusRef)
	: serviceRegistry(serviceRegistryRef), eventBus(eventBusRef)
{
}

bool CoreModule::Initialize()
{
	return true;
}

bool CoreModule::Start()
{
	return true;
}

void CoreModule::Stop()
{
}
} // namespace ow::modules
