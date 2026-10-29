#pragma once

#include <string>
#include <vector>

namespace ow::core
{
class IModule
{
public:
	virtual ~IModule() = default;
	virtual std::string GetId() const = 0;
	virtual std::vector<std::string> GetDependencies() const { return {}; }
	virtual bool Initialize() = 0;
	virtual bool Start() = 0;
	virtual void Stop() = 0;
};
} // namespace ow::core

