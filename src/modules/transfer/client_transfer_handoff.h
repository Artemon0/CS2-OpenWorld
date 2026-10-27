#pragma once

#include "transfer_manager.h"

#include "adapters/cs2/client_redirector.h"

#include <chrono>
#include <string>

namespace ow::transfer
{
class ClientTransferHandoff
{
public:
	ClientTransferHandoff(TransferManager& transfers, IVEngineServer2* engine)
		: transfers(transfers), engine(engine), redirector(engine)
	{
	}

	bool BeginRedirect(const std::string& transferId, CPlayerSlot slot, std::string_view endpoint,
					   std::chrono::system_clock::time_point now, std::string* error = nullptr);

private:
	TransferManager& transfers;
	IVEngineServer2* engine {};
	ow::adapters::ClientRedirector redirector;
};
} // namespace ow::transfer
