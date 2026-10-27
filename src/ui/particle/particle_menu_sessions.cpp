#include "particle_menu_sessions.h"

#include <cmath>
#include <utility>

namespace ow::ui::particle
{
bool ParticleMenuSessions::Open(std::uint64_t steamId, bool steamAuthenticated,
								std::vector<std::array<MenuItem, ParticleMenu::ItemsPerPage>> pages, float yaw, float pitch)
{
	if (steamId == 0 || !steamAuthenticated || !std::isfinite(yaw) || !std::isfinite(pitch))
	{
		return false;
	}

	Session session;
	if (!session.menu.SetPages(std::move(pages)))
	{
		return false;
	}
	session.lastYaw = yaw;
	session.lastPitch = pitch;
	sessions.insert_or_assign(steamId, std::move(session));
	return true;
}

bool ParticleMenuSessions::Close(std::uint64_t steamId)
{
	const auto found = sessions.find(steamId);
	if (found == sessions.end())
	{
		return false;
	}
	found->second.menu.Close();
	sessions.erase(found);
	return true;
}

bool ParticleMenuSessions::UpdateView(std::uint64_t steamId, float yaw, float pitch)
{
	const auto found = sessions.find(steamId);
	if (found == sessions.end() || !std::isfinite(yaw) || !std::isfinite(pitch))
	{
		return false;
	}

	float yawDelta = yaw - found->second.lastYaw;
	if (yawDelta > 180.0f)
	{
		yawDelta -= 360.0f;
	}
	else if (yawDelta < -180.0f)
	{
		yawDelta += 360.0f;
	}
	const float pitchDelta = pitch - found->second.lastPitch;
	found->second.lastYaw = yaw;
	found->second.lastPitch = pitch;
	found->second.menu.MoveCursor(yawDelta, pitchDelta);
	return true;
}

std::optional<MenuSelection> ParticleMenuSessions::HandleActivateInput(std::uint64_t steamId, bool pressed)
{
	const auto found = sessions.find(steamId);
	if (found == sessions.end())
	{
		return std::nullopt;
	}
	if (!pressed)
	{
		found->second.activateButtonDown = false;
		return std::nullopt;
	}
	if (found->second.activateButtonDown)
	{
		return std::nullopt;
	}
	found->second.activateButtonDown = true;
	return found->second.menu.ActivateHovered();
}

ParticleMenu* ParticleMenuSessions::Find(std::uint64_t steamId)
{
	const auto found = sessions.find(steamId);
	return found == sessions.end() ? nullptr : &found->second.menu;
}

const ParticleMenu* ParticleMenuSessions::Find(std::uint64_t steamId) const
{
	const auto found = sessions.find(steamId);
	return found == sessions.end() ? nullptr : &found->second.menu;
}

void ParticleMenuSessions::Clear()
{
	for (auto& entry : sessions)
	{
		entry.second.menu.Close();
	}
	sessions.clear();
}
} // namespace ow::ui::particle
