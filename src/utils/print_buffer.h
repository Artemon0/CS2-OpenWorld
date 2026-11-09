#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "utils/chat.h"

#if defined(__clang__) || defined(__GNUC__)
#define OW_CHAT_PRINTF_FORMAT(formatIndex, firstArgumentIndex) __attribute__((format(printf, formatIndex, firstArgumentIndex)))
#else
#define OW_CHAT_PRINTF_FORMAT(formatIndex, firstArgumentIndex)
#endif

// Short production helper for one formatted line of text. The output is truncated to
// MaxMessageBytes so a caller that cannot control the length (a player name, a database string)
// still gets a message instead of a silent rejection.
//
//   utils::chat::ToPlayer(slot, utils::chat::Format("%s joined the district.", name));
//
// The format argument must always be a string literal; never pass player input to it.
namespace utils::chat
{
class FormattedText
{
public:
	FormattedText(const char* format, ...) OW_CHAT_PRINTF_FORMAT(2, 3)
	{
		if (!format)
		{
			return;
		}
		va_list arguments;
		va_start(arguments, format);
		const int written = vsnprintf(buffer, sizeof(buffer), format, arguments);
		va_end(arguments);
		if (written < 0)
		{
			buffer[0] = '\0';
			return;
		}
		length = written < static_cast<int>(sizeof(buffer)) ? static_cast<std::size_t>(written) : MaxMessageBytes;
		buffer[length] = '\0';
	}

	// Implicit so a FormattedText can be passed straight to ToPlayer/ToConsole/ToChannel.
	operator std::string_view() const { return std::string_view(buffer, length); }

private:
	char buffer[MaxMessageBytes + 1] {};
	std::size_t length {};
};

template <typename... Arguments>
FormattedText Format(const char* format, Arguments... arguments) OW_CHAT_PRINTF_FORMAT(1, 2);

// The trailing pack keeps this a genuine function template, so the compiler checks the call
// against the printf attribute while the arguments are still forwarded to the constructor.
template <typename... Arguments>
inline FormattedText Format(const char* format, Arguments... arguments)
{
	return FormattedText(format, arguments...);
}
} // namespace utils::chat
