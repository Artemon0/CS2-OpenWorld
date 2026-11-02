#include "economy_system.h"

namespace ow::economy
{
EconomySystem::EconomySystem(ow::core::ServiceRegistry& serviceRegistryRef,
							 ow::adapters::PostgreSQLAdapter* persistenceRef)
	: serviceRegistry(serviceRegistryRef), persistence(persistenceRef)
{
}

bool EconomySystem::Initialize()
{
	return persistence != nullptr;
}

bool EconomySystem::Start()
{
	if (serviceRegistered)
	{
		return true;
	}
	serviceRegistered = serviceRegistry.Register<IEconomyService>(*this, 1);
	return serviceRegistered;
}

void EconomySystem::Stop()
{
	if (serviceRegistered)
	{
		serviceRegistry.Unregister<IEconomyService>(*this);
		serviceRegistered = false;
	}
}

std::optional<WalletSnapshot> EconomySystem::LoadWallet(std::string_view characterId)
{
	if (!persistence)
	{
		return std::nullopt;
	}
	const auto character = persistence->LoadCharacter(characterId);
	if (!character)
	{
		return std::nullopt;
	}
	return WalletSnapshot{character->balance, character->balanceRevision};
}

std::optional<TransactionResult> EconomySystem::ApplyTransaction(std::string_view characterId,
												  std::string_view idempotencyKey, std::int64_t amount,
												  std::string_view reason)
{
	if (!persistence)
	{
		return std::nullopt;
	}
	const auto result = persistence->ApplyMoneyTransaction(characterId, idempotencyKey, amount, reason);
	if (!result)
	{
		return std::nullopt;
	}
	return TransactionResult{result->balance, result->duplicate};
}

std::string EconomySystem::GetLastError() const
{
	return persistence ? persistence->GetLastError() : "PostgreSQL adapter is unavailable.";
}
} // namespace ow::economy
