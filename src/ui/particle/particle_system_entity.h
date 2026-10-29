#pragma once

#include "particle_constants.h"
#include "player/sdk/entity/cbasemodelentity.h"
#include "utils/schema.h"

class CParticleSystem : public CBaseModelEntity
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CParticleSystem);

	SCHEMA_FIELD(bool, m_bActive)
	SCHEMA_FIELD(bool, m_bStartActive)
	SCHEMA_FIELD(CUtlSymbolLarge, m_iszEffectName)
	SCHEMA_FIELD(bool, m_bNoSave)
	SCHEMA_FIELD(bool, m_bNoFreeze)
	SCHEMA_FIELD(bool, m_bNoRamp)

	void SetControlPoint(int slot, int cpIndex, const Vector& vec)
	{
		if (slot < 0 || slot >= 4)
		{
			return;
		}

		static const auto cpKey = schema::GetOffset(
			"CParticleSystem", hash_32_fnv1a_const("CParticleSystem"),
			"m_vServerControlPoints", hash_32_fnv1a_const("m_vServerControlPoints"));
		static const auto assignKey = schema::GetOffset(
			"CParticleSystem", hash_32_fnv1a_const("CParticleSystem"),
			"m_iServerControlPointAssignments", hash_32_fnv1a_const("m_iServerControlPointAssignments"));

		const uintptr_t pThis = reinterpret_cast<uintptr_t>(this);
		if (assignKey.offset != 0 && cpKey.offset != 0)
		{
			auto* assignments = reinterpret_cast<uint8_t*>(pThis + assignKey.offset);
			auto* points = reinterpret_cast<Vector*>(pThis + cpKey.offset);

			assignments[slot] = static_cast<uint8_t>(cpIndex);
			points[slot] = vec;

			::EntityNetworkStateChanged(pThis, assignKey.offset);
			::EntityNetworkStateChanged(pThis, cpKey.offset);
		}
	}

	void ApplyHudLayout(const ow::ui::particle::HudParticleLayout& layout)
	{
		// CP16: color tint
		SetControlPoint(0, 16, Vector(layout.colorR, layout.colorG, layout.colorB));
		// CP17: lifetime, alpha, current page + 1
		SetControlPoint(1, 17, Vector(1000000000.0f, 1.0f, static_cast<float>(layout.page + 1)));
		// CP33: (overlay X, overlay Y, virtual cursor X)
		SetControlPoint(2, 33, Vector(layout.x, layout.y, layout.cursorX));
		// CP34: (overlay scale, hover target selection, virtual cursor Y)
		SetControlPoint(3, 34, Vector(layout.scale, static_cast<float>(layout.selection), layout.cursorY));
	}

	void SendKillSignal()
	{
		// CP17 = (-1, 0, 0) kills the particle shader on client immediately
		SetControlPoint(1, 17, Vector(-1.0f, 0.0f, 0.0f));
	}
};
