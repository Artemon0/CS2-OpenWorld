#pragma once

#include <eiface.h>
#include <playerslot.h>

#include <string_view>

namespace ow::adapters
{
class ClientRedirector
{
public:
	explicit ClientRedirector(IVEngineServer2* engine) : engine(engine) {}

	static bool IsValidEndpoint(std::string_view endpoint);
	bool Redirect(CPlayerSlot slot, std::string_view endpoint) const;

private:
	IVEngineServer2* engine {};
};
} // namespace ow::adapters
