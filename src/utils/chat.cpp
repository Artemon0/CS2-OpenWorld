#include "chat.h"

#include "eiface.h"
#include "igameeventsystem.h"
#include "networksystem/inetworkmessages.h"
#include "playerslot.h"
#include "sdk/datatypes.h"
#include "sdk/recipientfilters.h"
#include "usermessages.pb.h"
#include "utils/interfaces.h"

#include <memory>

namespace utils::chat
{
namespace
{
// Same TextMsg payload the Panorama adapter sends, but for any recipient filter. Keeping it here
// makes utils independent of the adapter: a consumer only needs the engine interfaces.
bool SendMessage(IRecipientFilter& filter, int destination, std::string_view text)
{
	if (!interfaces::pGameEventSystem || !g_pNetworkMessages)
	{
		return false;
	}
	INetworkMessageInternal* networkMessage = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	if (!networkMessage)
	{
		return false;
	}
	std::unique_ptr<CNetMessage> message(networkMessage->AllocateMessage());
	if (!message)
	{
		return false;
	}
	CNetMessagePB<CUserMessageTextMsg>* textMessage = message->ToPB<CUserMessageTextMsg>();
	if (!textMessage)
	{
		return false;
	}

	textMessage->set_dest(destination);
	textMessage->add_param(std::string(text));
	textMessage->add_param("");
	textMessage->add_param("");
	textMessage->add_param("");
	textMessage->add_param("");
	interfaces::pGameEventSystem->PostEventAbstract(0, false, &filter, networkMessage, textMessage, 0);
	return true;
}

bool ValidateMessage(std::string_view text)
{
	return !text.empty() && text.size() <= MaxMessageBytes && text.find('\0') == std::string_view::npos;
}

// Engine console output used for the console channel and as the fallback. The payload always
// goes through "%s" so it can never be interpreted as a format string.
void WriteConsole(CPlayerSlot slot, std::string_view text)
{
	const std::string consoleLine = std::string(text) + '\n';
	interfaces::pEngine->ClientPrintf(slot, consoleLine.c_str());
}

bool SendToSlot(CPlayerSlot slot, int destination, std::string_view text)
{
	if (!slot.IsValid() || !interfaces::pEngine || !ValidateMessage(text))
	{
		return false;
	}
	CSingleRecipientFilter recipient(slot);
	if (destination == HUD_PRINTCONSOLE || !SendMessage(recipient, destination, text))
	{
		// The engine console works even when the network message system is unavailable.
		WriteConsole(slot, text);
	}
	return true;
}
} // namespace

bool ToChannel(CPlayerSlot slot, ow::ui::UITextChannel channel, std::string_view text)
{
	switch (channel)
	{
	case ow::ui::UITextChannel::Notification:
		return ToNotification(slot, text);
	case ow::ui::UITextChannel::Console:
		return ToConsole(slot, text);
	case ow::ui::UITextChannel::Chat:
		return ToPlayer(slot, text);
	case ow::ui::UITextChannel::Center:
		return ToCenter(slot, text);
	}
	return false;
}

bool ToPlayer(CPlayerSlot slot, std::string_view text)
{
	return SendToSlot(slot, HUD_PRINTTALK, text);
}



bool ToConsole(CPlayerSlot slot, std::string_view text)
{
	return SendToSlot(slot, HUD_PRINTCONSOLE, text);
}

bool ToCenter(CPlayerSlot slot, std::string_view text)
{
	return SendToSlot(slot, HUD_PRINTCENTER, text);
}

bool ToNotification(CPlayerSlot slot, std::string_view text)
{
	return SendToSlot(slot, HUD_PRINTNOTIFY, text);
}

bool Sanitize(std::string& text)
{
	if (text.find('\0') != std::string::npos)
	{
		return false;
	}
	for (char& character : text)
	{
		const auto byte = static_cast<unsigned char>(character);
		if (byte < 0x20 || byte == 0x7f)
		{
			character = ' ';
		}
	}
	return true;
}
} // namespace utils::chat
