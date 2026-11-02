#include "panorama_adapter.h"

#include <ISmmAPI.h>
#include <igameeventsystem.h>
#include "networksystem/inetworkmessages.h"
#include "player/sdk/datatypes.h"
#include "player/sdk/recipientfilters.h"
#include "usermessages.pb.h"

#include <algorithm>
#include <memory>
#include <string>

namespace ow::adapters
{
bool PanoramaAdapter::Initialize(SourceMM::ISmmAPI* metamodApi)
{
	Shutdown();
	if (!metamodApi)
	{
		return false;
	}

	CreateInterfaceFn engineFactory = metamodApi->GetEngineFactory();
	if (!engineFactory)
	{
		return false;
	}
	networkMessages = static_cast<INetworkMessages*>(
		metamodApi->VInterfaceMatch(engineFactory, NETWORKMESSAGES_INTERFACE_VERSION));
	gameEventSystem = static_cast<IGameEventSystem*>(
		metamodApi->VInterfaceMatch(engineFactory, GAMEEVENTSYSTEM_INTERFACE_VERSION));
	return IsInitialized();
}

void PanoramaAdapter::Shutdown()
{
	networkMessages = nullptr;
	gameEventSystem = nullptr;
}

bool PanoramaAdapter::IsInitialized() const
{
	return networkMessages && gameEventSystem;
}

ow::ui::UICapabilities PanoramaAdapter::GetCapabilities() const
{
	ow::ui::UICapabilities capabilities;
	const bool canSendText = IsInitialized() && networkMessages->FindNetworkMessagePartial("TextMsg");
	capabilities.textNotifications = canSendText;
	capabilities.perPlayerDelivery = canSendText;
	return capabilities;
}

ow::ui::UIStatus PanoramaAdapter::ShowText(CPlayerSlot slot, ow::ui::UITextChannel channel,
										  std::string_view text)
{
	if (!IsInitialized())
	{
		return ow::ui::UIStatus::Unavailable;
	}
	if (!slot.IsValid())
	{
		return ow::ui::UIStatus::InvalidPlayer;
	}
	int destination = 0;
	switch (channel)
	{
	case ow::ui::UITextChannel::Notification:
		destination = HUD_PRINTNOTIFY;
		break;
	case ow::ui::UITextChannel::Console:
		destination = HUD_PRINTCONSOLE;
		break;
	case ow::ui::UITextChannel::Chat:
		destination = HUD_PRINTTALK;
		break;
	case ow::ui::UITextChannel::Center:
		destination = HUD_PRINTCENTER;
		break;
	default:
		return ow::ui::UIStatus::InvalidPayload;
	}
	if (text.size() > MaxTextBytes || std::find(text.begin(), text.end(), '\0') != text.end())
	{
		return ow::ui::UIStatus::InvalidPayload;
	}

	INetworkMessageInternal* networkMessage = networkMessages->FindNetworkMessagePartial("TextMsg");
	if (!networkMessage)
	{
		return ow::ui::UIStatus::Unavailable;
	}

	std::unique_ptr<CNetMessage> message(networkMessage->AllocateMessage());
	if (!message)
	{
		return ow::ui::UIStatus::Unavailable;
	}
	CNetMessagePB<CUserMessageTextMsg>* textMessage = message->ToPB<CUserMessageTextMsg>();
	if (!textMessage)
	{
		return ow::ui::UIStatus::Unavailable;
	}

	textMessage->set_dest(destination);
	textMessage->add_param(std::string(text));
	textMessage->add_param("");
	textMessage->add_param("");
	textMessage->add_param("");
	textMessage->add_param("");

	CSingleRecipientFilter recipient(slot);
	gameEventSystem->PostEventAbstract(0, false, &recipient, networkMessage, textMessage, 0);
	return ow::ui::UIStatus::Success;
}
} // namespace ow::adapters
