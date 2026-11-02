#pragma once

#include "adapters/persistence/postgres_adapter.h"
#include "core/module.h"
#include "core/service_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ow::economy
{
struct WalletSnapshot
{
	std::int64_t balance {};
	std::uint64_t revision {};
};

struct TransactionResult
{
	std::int64_t balance {};
	bool duplicate {};
};

class IEconomyService
{
public:
	virtual ~IEconomyService() = default;
	virtual std::optional<WalletSnapshot> LoadWallet(std::string_view characterId) = 0;
	virtual std::optional<TransactionResult> ApplyTransaction(std::string_view characterId,
												  std::string_view idempotencyKey, std::int64_t amount,
												  std::string_view reason) = 0;
	virtual std::string GetLastError() const = 0;
};

class EconomySystem final : public ow::core::IModule, public IEconomyService
{
public:
	EconomySystem(ow::core::ServiceRegistry& serviceRegistry, ow::adapters::PostgreSQLAdapter* persistence);

	std::string GetId() const override { return "economy"; }
	bool Initialize() override;
	bool Start() override;
	void Stop() override;

	std::optional<WalletSnapshot> LoadWallet(std::string_view characterId) override;
	std::optional<TransactionResult> ApplyTransaction(std::string_view characterId,
											  std::string_view idempotencyKey, std::int64_t amount,
											  std::string_view reason) override;
	std::string GetLastError() const override;

private:
	ow::core::ServiceRegistry& serviceRegistry;
	ow::adapters::PostgreSQLAdapter* persistence {};
	bool serviceRegistered {};
};
} // namespace ow::economy
