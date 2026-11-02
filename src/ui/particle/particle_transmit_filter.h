#pragma once

#include "common.h"
#include "checktransmitinfo.h"
#include "eiface.h"

namespace ow::ui::particle
{
class ParticleTransmitFilter
{
public:
	static bool InstallHook(ISource2GameEntities* pGameEntities);
	static void RemoveHook(ISource2GameEntities* pGameEntities);

	static void RegisterMenuEntity(CBaseEntity* entity, int ownerPlayerSlot);
	static void UnregisterMenuEntity(int entityIndex);
	static void ClearPlayerEntities(int ownerPlayerSlot);
	static void ClearAll();

	static void OnCheckTransmit(
		CCheckTransmitInfo** pInfoInfoList, int nInfoCount,
		CBitVec<16384>& unionTransmitEdicts, CBitVec<16384>& unionTransmitAlways,
		const Entity2Networkable_t** pNetworkables, const uint16* pEntityIndicies, int nEntityIndices);
};
} // namespace ow::ui::particle
