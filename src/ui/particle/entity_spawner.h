#pragma once

#include "particle_system_entity.h"

namespace ow::ui::particle
{
class EntitySpawner
{
public:
	static bool Initialize();
	static CParticleSystem* SpawnParticleSystem(const char* effectName, const Vector& origin, const QAngle& angles, bool startActive);
	static void RemoveEntity(CEntityInstance* pEntity);
	static bool IsReady();
};
} // namespace ow::ui::particle
