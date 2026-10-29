#include "redis_cache.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace ow::persistence
{
namespace
{
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) { closesocket(socket); }

bool InitializeSockets()
{
	static const bool initialized = [] {
		WSADATA data {};
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return initialized;
}

bool IsConnectPending(int error)
{
	return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}
#else
using Socket = int;
constexpr Socket InvalidSocket = -1;
void CloseSocket(Socket socket) { close(socket); }

bool InitializeSockets() { return true; }
bool IsConnectPending(int error) { return error == EINPROGRESS; }
#endif

class SocketHandle
{
public:
	explicit SocketHandle(Socket socket) : socket(socket) {}
	~SocketHandle()
	{
		if (socket != InvalidSocket)
		{
			CloseSocket(socket);
		}
	}

	SocketHandle(const SocketHandle&) = delete;
	SocketHandle& operator=(const SocketHandle&) = delete;
	Socket Get() const { return socket; }

private:
	Socket socket;
};

struct ParsedReply
{
	char type {};
	std::string value;
	bool nil {};
};

struct IncompleteReply
{
};

std::string ReadLine(const std::string& data, std::size_t& offset)
{
	const std::size_t end = data.find("\r\n", offset);
	if (end == std::string::npos)
	{
		throw IncompleteReply{};
	}
	std::string result = data.substr(offset, end - offset);
	offset = end + 2;
	return result;
}

ParsedReply ParseReply(const std::string& data, std::size_t& offset)
{
	if (offset >= data.size())
	{
		throw IncompleteReply{};
	}
	const char type = data[offset++];
	if (type == '+' || type == '-' || type == ':')
	{
		return {type, ReadLine(data, offset)};
	}
	if (type == '$')
	{
		const std::string lengthText = ReadLine(data, offset);
		long long length {};
		const auto parsed = std::from_chars(lengthText.data(), lengthText.data() + lengthText.size(), length);
		if (parsed.ec != std::errc {} || parsed.ptr != lengthText.data() + lengthText.size() || length < -1 ||
			length > 16 * 1024 * 1024)
		{
			throw std::runtime_error("Redis returned an invalid bulk string length.");
		}
		if (length == -1)
		{
			return {'$', {}, true};
		}
		const std::size_t byteCount = static_cast<std::size_t>(length);
		if (byteCount > data.size() - offset || data.size() - offset - byteCount < 2)
		{
			throw IncompleteReply{};
		}
		if (data[offset + byteCount] != '\r' || data[offset + byteCount + 1] != '\n')
		{
			throw std::runtime_error("Redis returned a malformed bulk string.");
		}
		std::string value = data.substr(offset, byteCount);
		offset += byteCount + 2;
		return {'$', std::move(value)};
	}
	throw std::runtime_error("Redis returned an unsupported RESP2 reply.");
}

void AppendCommand(std::string& output, const std::vector<std::string>& command)
{
	output += '*' + std::to_string(command.size()) + "\r\n";
	for (const std::string& argument : command)
	{
		output += '$' + std::to_string(argument.size()) + "\r\n";
		output.append(argument);
		output += "\r\n";
	}
}

bool SetSocketTimeouts(Socket socket, std::chrono::milliseconds timeout)
{
#ifdef _WIN32
	const DWORD milliseconds = static_cast<DWORD>(timeout.count());
	return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds),
					  sizeof(milliseconds)) == 0 &&
		   setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&milliseconds),
					  sizeof(milliseconds)) == 0;
#else
	const timeval value{static_cast<time_t>(timeout.count() / 1000),
						static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
	return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) == 0 &&
		   setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) == 0;
#endif
}

bool ConnectWithTimeout(Socket socket, const sockaddr* address, std::size_t addressLength,
						std::chrono::milliseconds timeout)
{
#ifdef _WIN32
	u_long nonBlocking = 1;
	if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0)
	{
		return false;
	}
	const int result = connect(socket, address, static_cast<int>(addressLength));
	if (result != 0)
	{
		const int error = WSAGetLastError();
		if (!IsConnectPending(error))
		{
			return false;
		}
	}
	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(socket, &writeSet);
	timeval waitTime{static_cast<long>(timeout.count() / 1000),
					 static_cast<long>((timeout.count() % 1000) * 1000)};
	if (select(0, nullptr, &writeSet, nullptr, &waitTime) <= 0)
	{
		return false;
	}
	int socketError {};
	int socketErrorLength = sizeof(socketError);
	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength) != 0 ||
		socketError != 0)
	{
		return false;
	}
	nonBlocking = 0;
	return ioctlsocket(socket, FIONBIO, &nonBlocking) == 0;
#else
	const int previousFlags = fcntl(socket, F_GETFL, 0);
	if (previousFlags < 0 || fcntl(socket, F_SETFL, previousFlags | O_NONBLOCK) < 0)
	{
		return false;
	}
	const int result = connect(socket, address, static_cast<socklen_t>(addressLength));
	if (result != 0)
	{
		const int error = errno;
		if (!IsConnectPending(error))
		{
			return false;
		}
	}
	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(socket, &writeSet);
	timeval waitTime{static_cast<time_t>(timeout.count() / 1000),
					 static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
	if (select(socket + 1, nullptr, &writeSet, nullptr, &waitTime) <= 0)
	{
		return false;
	}
	int socketError {};
	socklen_t socketErrorLength = sizeof(socketError);
	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLength) != 0 || socketError != 0)
	{
		return false;
	}
	return fcntl(socket, F_SETFL, previousFlags) == 0;
#endif
}

bool SendAll(Socket socket, const std::string& data)
{
	std::size_t sent = 0;
	while (sent < data.size())
	{
#ifdef _WIN32
		const int result = send(socket, data.data() + sent,
								static_cast<int>((std::min)(data.size() - sent,
															 static_cast<std::size_t>((std::numeric_limits<int>::max)()))),
								0);
#else
		const ssize_t result = send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
		if (result <= 0)
		{
			return false;
		}
		sent += static_cast<std::size_t>(result);
	}
	return true;
}

std::optional<ParsedReply> ReceiveReply(Socket socket, std::string& error)
{
	std::string response;
	response.reserve(1024);
	char buffer[4096];
	while (response.size() <= 16 * 1024 * 1024)
	{
		std::size_t offset = 0;
		try
		{
			ParsedReply reply = ParseReply(response, offset);
			return reply;
		}
		catch (const IncompleteReply&)
		{
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return std::nullopt;
		}
		const int received = recv(socket, buffer, sizeof(buffer), 0);
		if (received <= 0)
		{
			return std::nullopt;
		}
		response.append(buffer, static_cast<std::size_t>(received));
	}
	return std::nullopt;
}
} // namespace

RedisCache::RedisCache(RedisCacheConfig config) : config(std::move(config))
{
	if (this->config.host.empty() || this->config.port == 0 || this->config.database < 0 ||
		this->config.keyPrefix.empty() || this->config.keyPrefix.size() > 512 ||
		this->config.timeout <= std::chrono::milliseconds::zero() || this->config.maxValueBytes == 0 ||
		this->config.maxValueBytes > 15 * 1024 * 1024)
	{
		throw std::invalid_argument("Redis cache configuration contains an invalid value.");
	}
}

std::optional<CacheEntry> RedisCache::Get(std::string_view key, std::uint64_t minimumRevision)
{
	if (key.empty() || key.size() > 1024)
	{
		SetLastError("Redis cache key is empty or exceeds 1024 bytes.");
		return std::nullopt;
	}
	bool isNil = false;
	const auto response = Execute({"GET", MakeKey(key)}, &isNil);
	if (!response)
	{
		return std::nullopt;
	}

	const std::size_t separator = response->find('\n');
	if (separator == std::string::npos)
	{
		SetLastError("Redis cache record is malformed: revision delimiter is missing.");
		return std::nullopt;
	}
	if (response->size() - separator - 1 > config.maxValueBytes)
	{
		SetLastError("Redis cache value exceeds the configured maximum size.");
		return std::nullopt;
	}
	std::uint64_t revision {};
	const auto parsed = std::from_chars(response->data(), response->data() + separator, revision);
	if (parsed.ec != std::errc {} || parsed.ptr != response->data() + separator || revision == 0)
	{
		SetLastError("Redis cache record contains an invalid revision.");
		return std::nullopt;
	}
	if (revision < minimumRevision)
	{
		SetLastError({});
		return std::nullopt;
	}
	SetLastError({});
	return CacheEntry{response->substr(separator + 1), revision,
					  (std::chrono::steady_clock::time_point::max)()};
}

bool RedisCache::Put(std::string key, std::string value, std::uint64_t revision, std::chrono::seconds ttl)
{
	if (key.empty() || key.size() > 1024 || revision == 0 || ttl <= std::chrono::seconds::zero() ||
		value.size() > config.maxValueBytes ||
		ttl.count() > (std::numeric_limits<std::int64_t>::max)() / 1000)
	{
		SetLastError("Redis cache write has an invalid key, revision, value size, or TTL.");
		return false;
	}

	static const std::string script =
		"local old=redis.call('GET',KEYS[1]); "
		"if old then "
		"local split=string.find(old,'\\n',1,true); "
		"if not split then return redis.error_reply('malformed cache revision') end; "
		"local current=string.sub(old,1,split-1); "
		"if not string.match(current,'^%d+$') then return redis.error_reply('invalid cache revision') end; "
		"local incoming=ARGV[1]; "
		"if string.len(current)>string.len(incoming) or "
		"(string.len(current)==string.len(incoming) and current>incoming) then return 0 end; "
		"end; "
		"redis.call('SET',KEYS[1],ARGV[1]..'\\n'..ARGV[2],'PX',ARGV[3]); "
		"return 1";
	const auto response = Execute({"EVAL", script, "1", MakeKey(key), std::to_string(revision), std::move(value),
								   std::to_string(ttl.count() * 1000)});
	if (!response)
	{
		return false;
	}
	if (*response == "1")
	{
		SetLastError({});
		return true;
	}
	if (*response == "0")
	{
		SetLastError("Redis cache rejected a write with an older revision.");
		return false;
	}
	SetLastError("Redis returned an unexpected response to a cache write.");
	return false;
}

bool RedisCache::Invalidate(std::string_view key)
{
	if (key.empty() || key.size() > 1024)
	{
		SetLastError("Redis cache key is empty or exceeds 1024 bytes.");
		return false;
	}
	const auto response = Execute({"DEL", MakeKey(key)});
	if (!response)
	{
		return false;
	}
	SetLastError({});
	return *response == "1";
}

bool RedisCache::Ping()
{
	const auto response = Execute({"PING"});
	if (!response)
	{
		return false;
	}
	const bool success = *response == "PONG";
	SetLastError(success ? std::string{} : "Redis returned an unexpected response to PING.");
	return success;
}

std::string RedisCache::GetLastError() const
{
	std::lock_guard lock(errorMutex);
	return lastError;
}

std::optional<std::string> RedisCache::Execute(const std::vector<std::string>& command, bool* isNil)
{
	if (isNil)
	{
		*isNil = false;
	}
	if (!InitializeSockets())
	{
		SetLastError("Socket subsystem initialization failed.");
		return std::nullopt;
	}

	addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo* addresses = nullptr;
	const std::string port = std::to_string(config.port);
	const int resolveResult = getaddrinfo(config.host.c_str(), port.c_str(), &hints, &addresses);
	if (resolveResult != 0)
	{
		SetLastError("Could not resolve Redis host.");
		return std::nullopt;
	}

	Socket connectedSocket = InvalidSocket;
	for (addrinfo* address = addresses; address; address = address->ai_next)
	{
		Socket candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (candidate == InvalidSocket)
		{
			continue;
		}
		if (ConnectWithTimeout(candidate, address->ai_addr, address->ai_addrlen, config.timeout) &&
			SetSocketTimeouts(candidate, config.timeout))
		{
			connectedSocket = candidate;
			break;
		}
		CloseSocket(candidate);
	}
	freeaddrinfo(addresses);
	if (connectedSocket == InvalidSocket)
	{
		SetLastError("Could not connect to Redis within the configured timeout.");
		return std::nullopt;
	}
	SocketHandle connected(connectedSocket);

	auto sendCommand = [&](const std::vector<std::string>& arguments) -> std::optional<ParsedReply> {
		std::string request;
		AppendCommand(request, arguments);
		if (!SendAll(connected.Get(), request))
		{
			SetLastError("Failed to send a Redis command.");
			return std::nullopt;
		}
		std::string parseError;
		auto reply = ReceiveReply(connected.Get(), parseError);
		if (!reply)
		{
			SetLastError(parseError.empty() ? "Failed to read a complete Redis response." : std::move(parseError));
			return std::nullopt;
		}
		if (reply->type == '-')
		{
			SetLastError("Redis error: " + reply->value);
			return std::nullopt;
		}
		return reply;
	};

	if (!config.password.empty())
	{
		const auto auth = config.username.empty()
							  ? sendCommand({"AUTH", config.password})
							  : sendCommand({"AUTH", config.username, config.password});
		if (!auth || auth->type != '+' || auth->value != "OK")
		{
			if (auth)
			{
				SetLastError("Redis authentication was not accepted.");
			}
			return std::nullopt;
		}
	}
	if (config.database != 0)
	{
		const auto selected = sendCommand({"SELECT", std::to_string(config.database)});
		if (!selected || selected->type != '+' || selected->value != "OK")
		{
			if (selected)
			{
				SetLastError("Redis database selection failed.");
			}
			return std::nullopt;
		}
	}

	const auto reply = sendCommand(command);
	if (!reply)
	{
		return std::nullopt;
	}
	if (reply->type == '$' && reply->nil && isNil)
	{
		*isNil = true;
		SetLastError({});
		return std::nullopt;
	}
	if (reply->type != '$' && reply->type != '+' && reply->type != ':')
	{
		SetLastError("Redis returned an unexpected reply type.");
		return std::nullopt;
	}
	SetLastError({});
	return reply->value;
}

std::string RedisCache::MakeKey(std::string_view key) const
{
	std::string redisKey = config.keyPrefix;
	redisKey.append(key);
	return redisKey;
}

void RedisCache::SetLastError(std::string message) const
{
	std::lock_guard lock(errorMutex);
	lastError = std::move(message);
}
} // namespace ow::persistence
