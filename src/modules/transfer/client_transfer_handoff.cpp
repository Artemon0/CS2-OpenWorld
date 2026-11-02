#include "client_transfer_handoff.h"
#include "utils/engine_server_compat.h"

namespace ow::transfer
{
namespace
{
void SetError(std::string* error, const char* message)
{
	if (error)
	{
		*error = message;
	}
}
} // namespace

bool ClientTransferHandoff::BeginRedirect(const std::string& transferId, CPlayerSlot slot, std::string_view endpoint,
										  std::chrono::system_clock::time_point now, std::string* error)
{
	const auto transfer = transfers.Find(transferId);
	if (!transfer || transfer->state != TransferState::StateSaved)
	{
		SetError(error, "Transfer is not ready for client redirection.");
		return false;
	}
	if (!engine)
	{
		SetError(error, "The CS2 engine interface is unavailable.");
		return false;
	}
	if (!ow::adapters::ClientRedirector::IsValidEndpoint(endpoint))
	{
		SetError(error, "Destination endpoint is invalid.");
		return false;
	}

	const CSteamID* steamId = utils::GetClientSteamID(engine, slot);
	if (!steamId || !steamId->IsValid() || !utils::IsClientFullyAuthenticated(engine, slot) ||
		steamId->ConvertToUint64() != transfer->snapshot.steamId)
	{
		SetError(error, "Player Steam authorization does not match the transfer record.");
		return false;
	}
	if (now >= transfer->expiresAt)
	{
		transfers.Advance(transferId, TransferState::Redirecting, now);
		SetError(error, "Transfer has expired.");
		return false;
	}
	if (!transfers.Advance(transferId, TransferState::Redirecting, now))
	{
		SetError(error, "Transfer could not enter the redirecting state.");
		return false;
	}
	if (!redirector.Redirect(slot, endpoint))
	{
		transfers.Fail(transferId, now);
		SetError(error, "CS2 did not accept the client redirect request.");
		return false;
	}

	if (error)
	{
		error->clear();
	}
	return true;
}
} // namespace ow::transfer
