#pragma once

#include <string_view>

namespace ow::core
{
class IModule
{
public:
	virtual ~IModule() = default;
	virtual std::string_view GetId() const = 0;
	virtual bool Initialize() = 0;
	virtual bool Start() = 0;
	virtual void Stop() = 0;
};
} // namespace ow::core
