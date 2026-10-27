#pragma once

#include <string_view>
#include <vector>

namespace ow::core
{
class IModule
{
public:
	virtual ~IModule() = default;
	virtual std::string_view GetId() const = 0;
	virtual std::vector<std::string_view> GetDependencies() const { return {}; }
	virtual bool Initialize() = 0;
	virtual bool Start() = 0;
	virtual void Stop() = 0;
};
} // namespace ow::core
