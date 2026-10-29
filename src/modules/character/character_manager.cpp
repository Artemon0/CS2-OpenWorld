#include "character_manager.h"

namespace ow::modules
{
CharacterManager::CharacterManager(ow::core::ServiceRegistry& serviceRegistryRef, ow::core::EventBus& eventBusRef,
								   ow::adapters::PostgreSQLAdapter* persistencePtr)
	: serviceRegistry(serviceRegistryRef), eventBus(eventBusRef), persistence(persistencePtr)
{
}

bool CharacterManager::Initialize()
{
	return true;
}

bool CharacterManager::Start()
{
	return true;
}

void CharacterManager::Stop()
{
}
} // namespace ow::modules
