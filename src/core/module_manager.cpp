#include "module_manager.h"

#include <algorithm>

namespace ow::core
{
bool ModuleManager::RegisterModule(IModule& module)
{
	if (initializedCount != 0 || module.GetId().empty())
	{
		return false;
	}

	const auto duplicate = std::find_if(modules.begin(), modules.end(), [&module](const IModule* existing) {
		return existing->GetId() == module.GetId();
	});
	if (duplicate != modules.end())
	{
		return false;
	}

	modules.push_back(&module);
	return true;
}

bool ModuleManager::StartAll()
{
	if (initializedCount != 0)
	{
		return startedCount == modules.size();
	}

	for (IModule* module : modules)
	{
		if (!module->Initialize())
		{
			module->Stop();
			StopAll();
			return false;
		}
		++initializedCount;
		if (!module->Start())
		{
			StopAll();
			return false;
		}
		++startedCount;
	}
	return true;
}

void ModuleManager::StopAll()
{
	startedCount = 0;
	while (initializedCount > 0)
	{
		modules[--initializedCount]->Stop();
	}
}
} // namespace ow::core
