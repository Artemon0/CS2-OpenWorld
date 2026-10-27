#pragma once

#include "module.h"

#include <cstddef>
#include <string>
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
	const std::string& GetLastError() const { return lastError; }

private:
	bool ResolveStartupOrder();
	void RollBack();

	std::vector<IModule*> modules;
	std::vector<IModule*> startupOrder;
	std::vector<IModule*> initializedModules;
	std::size_t startedCount {};
	std::string lastError;
};
} // namespace ow::core
