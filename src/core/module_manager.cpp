#include "module_manager.h"

#include <algorithm>
#include <functional>

namespace ow::core
{
bool ModuleManager::RegisterModule(IModule& module)
{
	if (!initializedModules.empty() || module.GetId().empty())
	{
		lastError = "Cannot register a module after startup or with an empty ID.";
		return false;
	}

	const auto duplicate = std::find_if(modules.begin(), modules.end(), [&module](const IModule* existing) {
		return existing->GetId() == module.GetId();
	});
	if (duplicate != modules.end())
	{
		lastError = "A module with the same ID is already registered.";
		return false;
	}

	modules.push_back(&module);
	lastError.clear();
	return true;
}

bool ModuleManager::StartAll()
{
	if (!initializedModules.empty())
	{
		return startedCount == modules.size();
	}

	if (!ResolveStartupOrder())
	{
		return false;
	}

	for (IModule* module : startupOrder)
	{
		if (!module->Initialize())
		{
			lastError = "Module initialization failed: " + std::string(module->GetId());
			module->Stop();
			RollBack();
			return false;
		}
		initializedModules.push_back(module);

		if (!module->Start())
		{
			lastError = "Module start failed: " + std::string(module->GetId());
			module->Stop();
			initializedModules.pop_back();
			RollBack();
			return false;
		}
		++startedCount;
	}

	lastError.clear();
	return true;
}

void ModuleManager::StopAll()
{
	RollBack();
}

bool ModuleManager::ResolveStartupOrder()
{
	enum class VisitState
	{
		Unvisited,
		Visiting,
		Visited
	};

	std::vector<VisitState> states(modules.size(), VisitState::Unvisited);
	std::vector<IModule*> ordered;
	std::function<bool(std::size_t)> visit = [&](std::size_t index) {
		if (states[index] == VisitState::Visited)
		{
			return true;
		}
		if (states[index] == VisitState::Visiting)
		{
			lastError = "Module dependency cycle includes: " + std::string(modules[index]->GetId());
			return false;
		}

		states[index] = VisitState::Visiting;
		for (std::string_view dependency : modules[index]->GetDependencies())
		{
			const auto found = std::find_if(modules.begin(), modules.end(), [dependency](const IModule* candidate) {
				return candidate->GetId() == dependency;
			});
			if (found == modules.end())
			{
				lastError = "Missing module dependency '" + std::string(dependency) + "' required by '" +
							std::string(modules[index]->GetId()) + "'.";
				return false;
			}
			const auto dependencyIndex = static_cast<std::size_t>(std::distance(modules.begin(), found));
			if (!visit(dependencyIndex))
			{
				return false;
			}
		}

		states[index] = VisitState::Visited;
		ordered.push_back(modules[index]);
		return true;
	};

	for (std::size_t i = 0; i < modules.size(); ++i)
	{
		if (!visit(i))
		{
			return false;
		}
	}

	startupOrder = std::move(ordered);
	lastError.clear();
	return true;
}

void ModuleManager::RollBack()
{
	while (!initializedModules.empty())
	{
		initializedModules.back()->Stop();
		initializedModules.pop_back();
	}
	startedCount = 0;
}
} // namespace ow::core
