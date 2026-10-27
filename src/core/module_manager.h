#pragma once

#include "module.h"

#include <cstddef>
#include <vector>

namespace ow::core
{
class ModuleManager
{
public:
	bool RegisterModule(IModule& module);
	bool StartAll();
	void StopAll();
	bool IsRunning() const { return startedCount == modules.size(); }

private:
	std::vector<IModule*> modules;
	std::size_t initializedCount {};
	std::size_t startedCount {};
};
} // namespace ow::core
