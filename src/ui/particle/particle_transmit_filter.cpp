#include "particle_transmit_filter.h"

#include "player/sdk/entity/cbaseentity.h"
#include "player/sdk/serversideclient.h"
#include "utils/utils.h"

#include <khook.hpp>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ow::ui::particle
{
namespace
{
struct RegisteredEntity
{
	CHandle<CBaseEntity> entity;
	int ownerPlayerSlot {-1};
};

std::mutex s_Mutex;
std::unordered_map<int, RegisteredEntity> s_RegisteredEntities;

KHook::Return<void> HookCheckTransmit(
	ISource2GameEntities*, CCheckTransmitInfo** pInfoInfoList, int nInfoCount,
	CBitVec<16384>& unionTransmitEdicts, CBitVec<16384>& unionTransmitAlways,
	const Entity2Networkable_t** pNetworkables, const uint16* pEntityIndicies, int nEntityIndices)
{
	ParticleTransmitFilter::OnCheckTransmit(
		pInfoInfoList, nInfoCount, unionTransmitEdicts, unionTransmitAlways,
		pNetworkables, pEntityIndicies, nEntityIndices);
	return {KHook::Action::Ignore};
}

KHook::Virtual<ISource2GameEntities, void, CCheckTransmitInfo**, int, CBitVec<16384>&,
			   CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int>
	checkTransmitHook{&ISource2GameEntities::CheckTransmit, nullptr, &HookCheckTransmit};

bool hookInstalled = false;
ISource2GameEntities* hookedGameEntities = nullptr;
} // namespace

bool ParticleTransmitFilter::InstallHook(ISource2GameEntities* pGameEntities)
{
	if (!pGameEntities || hookInstalled || !g_pCS2ACUtils || !g_pCS2ACUtils->GetClientList())
	{
		return hookInstalled;
	}

	checkTransmitHook.Add(pGameEntities);
	hookInstalled = checkTransmitHook.IsActive();
	if (!hookInstalled)
	{
		checkTransmitHook.Remove(pGameEntities);
		return false;
	}
	hookedGameEntities = pGameEntities;
	return true;
}

void ParticleTransmitFilter::RemoveHook(ISource2GameEntities* pGameEntities)
{
	if (!hookInstalled)
	{
		ClearAll();
		return;
	}

	ISource2GameEntities* target = hookedGameEntities ? hookedGameEntities : pGameEntities;
	if (!target)
	{
		return;
	}
	checkTransmitHook.Remove(target);
	hookedGameEntities = nullptr;
	hookInstalled = false;
	ClearAll();
}

void ParticleTransmitFilter::RegisterMenuEntity(CBaseEntity* entity, int ownerPlayerSlot)
{
	if (!entity || ownerPlayerSlot < 0 || ownerPlayerSlot >= MAXPLAYERS)
	{
		return;
	}
	const int entityIndex = entity->entindex();
	if (entityIndex <= 0 || entityIndex >= MAX_EDICTS)
	{
		return;
	}

	RegisteredEntity registered;
	registered.entity = entity;
	registered.ownerPlayerSlot = ownerPlayerSlot;
	std::lock_guard<std::mutex> lock(s_Mutex);
	s_RegisteredEntities.insert_or_assign(entityIndex, std::move(registered));
}

void ParticleTransmitFilter::UnregisterMenuEntity(int entityIndex)
{
	if (entityIndex <= 0)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(s_Mutex);
	s_RegisteredEntities.erase(entityIndex);
}

void ParticleTransmitFilter::ClearPlayerEntities(int ownerPlayerSlot)
{
	std::lock_guard<std::mutex> lock(s_Mutex);
	for (auto it = s_RegisteredEntities.begin(); it != s_RegisteredEntities.end();)
	{
		if (it->second.ownerPlayerSlot == ownerPlayerSlot)
		{
			it = s_RegisteredEntities.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void ParticleTransmitFilter::ClearAll()
{
	std::lock_guard<std::mutex> lock(s_Mutex);
	s_RegisteredEntities.clear();
}

void ParticleTransmitFilter::OnCheckTransmit(
	CCheckTransmitInfo** pInfoInfoList, int nInfoCount,
	CBitVec<16384>&, CBitVec<16384>&,
	const Entity2Networkable_t**, const uint16*, int)
{
	std::lock_guard<std::mutex> lock(s_Mutex);
	if (s_RegisteredEntities.empty() || !pInfoInfoList || nInfoCount <= 0 || nInfoCount > MAXPLAYERS)
	{
		return;
	}

	for (int i = 0; i < nInfoCount; ++i)
	{
		CCheckTransmitInfo* pInfo = pInfoInfoList[i];
		if (!pInfo || !pInfo->m_pTransmitEntity)
		{
			continue;
		}

		int clientSlot = -1;
		if (g_pCS2ACUtils)
		{
			for (int slot = 0; slot < MAXPLAYERS; ++slot)
			{
				CServerSideClient* pClient = g_pCS2ACUtils->GetClientBySlot(CPlayerSlot(slot));
				if (pClient && &pClient->m_PackInfo == pInfo)
				{
					clientSlot = slot;
					break;
				}
			}
		}

		for (auto entity = s_RegisteredEntities.begin(); entity != s_RegisteredEntities.end();)
		{
			if (!entity->second.entity.Get())
			{
				entity = s_RegisteredEntities.erase(entity);
				continue;
			}
			if (clientSlot != entity->second.ownerPlayerSlot && entity->first > 0 && entity->first < MAX_EDICTS)
			{
				pInfo->m_pTransmitEntity->Clear(entity->first);
			}
			++entity;
		}
	}
}
} // namespace ow::ui::particle
