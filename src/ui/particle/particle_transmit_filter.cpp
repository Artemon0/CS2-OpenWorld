#include "particle_transmit_filter.h"

#include "player/sdk/serversideclient.h"
#include "utils/utils.h"

#include <khook.hpp>
#include <mutex>
#include <unordered_map>

namespace ow::ui::particle
{
namespace
{
std::mutex s_Mutex;
std::unordered_map<int, int> s_RegisteredEntities; // entityIndex -> ownerPlayerSlot

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
} // namespace

bool ParticleTransmitFilter::InstallHook(ISource2GameEntities* pGameEntities)
{
	if (!pGameEntities || hookInstalled)
	{
		return hookInstalled;
	}

	checkTransmitHook.Add(pGameEntities);
	hookInstalled = true;
	return true;
}

void ParticleTransmitFilter::RemoveHook(ISource2GameEntities* pGameEntities)
{
	if (!pGameEntities || !hookInstalled)
	{
		return;
	}

	checkTransmitHook.Remove(pGameEntities);
	hookInstalled = false;
	ClearAll();
}

void ParticleTransmitFilter::RegisterMenuEntity(int entityIndex, int ownerPlayerSlot)
{
	if (entityIndex <= 0)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(s_Mutex);
	s_RegisteredEntities[entityIndex] = ownerPlayerSlot;
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
		if (it->second == ownerPlayerSlot)
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
	if (s_RegisteredEntities.empty() || !pInfoInfoList)
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

		for (const auto& [entityIndex, ownerSlot] : s_RegisteredEntities)
		{
			if (entityIndex <= 0 || entityIndex >= MAX_EDICTS)
			{
				continue;
			}

			// If this client is NOT the owner of the particle menu, clear it from their snapshot
			if (clientSlot != ownerSlot)
			{
				pInfo->m_pTransmitEntity->Clear(entityIndex);
			}
		}
	}
}
} // namespace ow::ui::particle
