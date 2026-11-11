#include "client_redirector.h"

#include <cctype>
#include <string>

namespace ow::adapters
{
namespace
{
bool IsPort(std::string_view text)
{
	if (text.empty() || text.size() > 5)
	{
		return false;
	}

	unsigned int port = 0;
	for (const char digit : text)
	{
		if (digit < '0' || digit > '9' || !std::isdigit(digit))
		{
			return false;
		}
		port = port * 10 + static_cast<unsigned int>(digit - '0');
	}
	return port > 0 && port <= 65535;
}

bool IsDnsOrIPv4(std::string_view host)
{
	if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.')
	{
		return false;
	}

	std::size_t labelStart = 0;
	while (labelStart < host.size())
	{
		const std::size_t separator = host.find('.', labelStart);
		const std::size_t labelEnd = separator == std::string_view::npos ? host.size() : separator;
		const std::string_view label = host.substr(labelStart, labelEnd - labelStart);
		if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-')
		{
			return false;
		}
		for (const unsigned char character : label)
		{
			if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				  (character >= '0' && character <= '9') || character == '-'))
			{
				return false;
			}
		}
		if (separator == std::string_view::npos)
		{
			break;
		}
		labelStart = separator + 1;
	}
	return true;
}

bool IsBracketedIPv6(std::string_view host)
{
	if (host.size() < 4 || host.front() != '[' || host.back() != ']')
	{
		return false;
	}

	const std::string_view address = host.substr(1, host.size() - 2);
	const std::size_t compression = address.find("::");
	if (compression != std::string_view::npos && address.find("::", compression + 2) != std::string_view::npos)
	{
		return false;
	}

	const auto countGroups = [](std::string_view side) -> int {
		if (side.empty())
		{
			return 0;
		}
		if (side.front() == ':' || side.back() == ':')
		{
			return -1;
		}

		int groups = 0;
		std::size_t start = 0;
		while (start < side.size())
		{
			const std::size_t separator = side.find(':', start);
			const std::size_t end = separator == std::string_view::npos ? side.size() : separator;
			const std::string_view group = side.substr(start, end - start);
			if (group.empty() || group.size() > 4)
			{
				return -1;
			}
			for (const unsigned char character : group)
			{
				if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
					  (character >= 'A' && character <= 'F')))
				{
					return -1;
				}
			}
			++groups;
			if (separator == std::string_view::npos)
			{
				break;
			}
			start = separator + 1;
		}
		return groups;
	};

	if (compression == std::string_view::npos)
	{
		return countGroups(address) == 8;
	}

	const int leftGroups = countGroups(address.substr(0, compression));
	const int rightGroups = countGroups(address.substr(compression + 2));
	return leftGroups >= 0 && rightGroups >= 0 && leftGroups + rightGroups < 8;
}
} // namespace

bool ClientRedirector::IsValidEndpoint(std::string_view endpoint)
{
	if (endpoint.empty() || endpoint.size() > 260)
	{
		return false;
	}

	std::string_view host;
	std::string_view port;
	if (endpoint.front() == '[')
	{
		const std::size_t closeBracket = endpoint.find(']');
		if (closeBracket == std::string_view::npos || closeBracket + 1 >= endpoint.size() ||
			endpoint[closeBracket + 1] != ':')
		{
			return false;
		}
		host = endpoint.substr(0, closeBracket + 1);
		port = endpoint.substr(closeBracket + 2);
		if (!IsBracketedIPv6(host))
		{
			return false;
		}
	}
	else
	{
		const std::size_t separator = endpoint.rfind(':');
		if (separator == std::string_view::npos || endpoint.find(':') != separator)
		{
			return false;
		}
		host = endpoint.substr(0, separator);
		port = endpoint.substr(separator + 1);
		if (!IsDnsOrIPv4(host))
		{
			return false;
		}
	}
	return IsPort(port);
}

bool ClientRedirector::Redirect(CPlayerSlot slot, std::string_view endpoint) const
{
	if (!engine || !IsValidEndpoint(endpoint))
	{
		return false;
	}
	const std::string commandTarget(endpoint);
	engine->ClientCommand(slot, "connect %s", commandTarget.c_str());
	return true;
}
} // namespace ow::adapters
