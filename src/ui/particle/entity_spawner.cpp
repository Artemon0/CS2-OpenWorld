#include "entity_spawner.h"

#include "entity2/entitysystem.h"
#include "utils/addresses.h"
#include "utils/gameconfig.h"
#include "utils/utils.h"

namespace ow::ui::particle
{
namespace
{
using CreateEntityByNameFn = CEntityInstance* (*)(void* pEntitySystem, const char* pClassName, int iForceEdictIndex);
using DispatchSpawnFn = void (*)(CEntityInstance* pEntity, void* pKeyValues);
using UTIL_RemoveFn = void (*)(CEntityInstance* pEntity);

CreateEntityByNameFn fnCreateEntityByName = nullptr;
DispatchSpawnFn fnDispatchSpawn = nullptr;
UTIL_RemoveFn fnUTIL_Remove = nullptr;
bool signaturesResolved = false;

void ResolveSignatures()
{
	if (!modules::server || signaturesResolved)
	{
		return;
	}

	int sigError = 0;
#ifdef _WIN32
	// Windows signatures for CreateEntityByName
	const byte sigCreate1[] = "\x48\x83\xEC\x48\xC6\x44\x24\x30\x00";
	const byte sigCreate2[] = "\x48\x83\xEC\x2A\xC6\x44\x24\x2A\x2A\x4C\x8B\xC1";
	void* addrCreate = modules::server->FindSignature(sigCreate1, sizeof(sigCreate1) - 1, sigError);
	if (!addrCreate)
	{
		addrCreate = modules::server->FindSignature(sigCreate2, sizeof(sigCreate2) - 1, sigError);
	}
	fnCreateEntityByName = reinterpret_cast<CreateEntityByNameFn>(addrCreate);

	// Windows signature for DispatchSpawn
	const byte sigSpawn[] = "\x48\x89\x5C\x24\x10\x57\x48\x83\xEC\x30\x48\x8B\xDA\x48\x8B\xF9";
	fnDispatchSpawn = reinterpret_cast<DispatchSpawnFn>(
		modules::server->FindSignature(sigSpawn, sizeof(sigSpawn) - 1, sigError));

	// Windows signature for UTIL_Remove
	const byte sigRemove[] = "\x48\x85\xC9\x74\x2A\x48\x8B\xD1\x48\x8B\x0D";
	fnUTIL_Remove = reinterpret_cast<UTIL_RemoveFn>(
		modules::server->FindSignature(sigRemove, sizeof(sigRemove) - 1, sigError));
#else
	// Linux signature for CreateEntityByName
	const byte sigCreate[] = "\x48\x8D\x05\x2A\x2A\x2A\x2A\x55\x48\x89\xFA";
	fnCreateEntityByName = reinterpret_cast<CreateEntityByNameFn>(
		modules::server->FindSignature(sigCreate, sizeof(sigCreate) - 1, sigError));

	// Linux signature for DispatchSpawn
	const byte sigSpawn[] = "\x48\x85\xFF\x74\x2A\x55\x48\x89\xE5\x41\x55\x49\x89\xFD";
	fnDispatchSpawn = reinterpret_cast<DispatchSpawnFn>(
		modules::server->FindSignature(sigSpawn, sizeof(sigSpawn) - 1, sigError));

	// Linux signature for UTIL_Remove
	const byte sigRemove[] = "\x48\x89\xFE\x48\x85\xFF\x74\x2A\x48\x8D\x05";
	fnUTIL_Remove = reinterpret_cast<UTIL_RemoveFn>(
		modules::server->FindSignature(sigRemove, sizeof(sigRemove) - 1, sigError));
#endif

	signaturesResolved = true;
}
} // namespace

bool EntitySpawner::Initialize()
{
	ResolveSignatures();
	return IsReady();
}

bool EntitySpawner::IsReady()
{
	if (!signaturesResolved)
	{
		ResolveSignatures();
	}
	return fnCreateEntityByName && fnDispatchSpawn && fnUTIL_Remove;
}

CParticleSystem* EntitySpawner::SpawnParticleSystem(
	const char* effectName, const Vector& origin, const QAngle& angles, bool startActive)
{
	if (!IsReady())
	{
		return nullptr;
	}

	CGameEntitySystem* entitySystem = GameEntitySystem();
	if (!entitySystem)
	{
		return nullptr;
	}

	CEntityInstance* rawInstance = fnCreateEntityByName(entitySystem, "info_particle_system", -1);
	if (!rawInstance)
	{
		return nullptr;
	}

	auto* particle = static_cast<CParticleSystem*>(rawInstance);

	// Configure particle attributes via schema
	particle->m_bNoSave(true);
	particle->m_bNoFreeze(true);
	particle->m_bNoRamp(true);
	particle->m_bStartActive(startActive);
	particle->m_bActive(startActive);

	if (effectName && effectName[0] != '\0')
	{
		particle->m_iszEffectName(CUtlSymbolLarge(effectName));
	}

	// Teleport to starting location and orientation
	particle->Teleport(&origin, &angles, nullptr);

	fnDispatchSpawn(particle, nullptr);

	return particle;
}

void EntitySpawner::RemoveEntity(CEntityInstance* pEntity)
{
	if (!pEntity)
	{
		return;
	}

	if (fnUTIL_Remove)
	{
		fnUTIL_Remove(pEntity);
	}
}
} // namespace ow::ui::particle
