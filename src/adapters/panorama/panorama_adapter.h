#pragma once

#include "ui/ui_adapter.h"

#include <cstddef>

namespace SourceMM
{
class ISmmAPI;
}

class IGameEventSystem;
class INetworkMessages;
class INetworkMessageInternal;

namespace ow::adapters
{
class PanoramaAdapter final : public ow::ui::IUIAdapter
{
public:
	static constexpr std::size_t MaxTextBytes = 1024;

	bool Initialize(SourceMM::ISmmAPI* metamodApi);
	void Shutdown();
	bool IsInitialized() const;

	ow::ui::UICapabilities GetCapabilities() const override;
	ow::ui::UIStatus ShowText(CPlayerSlot slot, ow::ui::UITextChannel channel,
						  std::string_view text) override;

private:
	INetworkMessages* networkMessages {};
	IGameEventSystem* gameEventSystem {};
	INetworkMessageInternal* textMessage {};
};
} // namespace ow::adapters
