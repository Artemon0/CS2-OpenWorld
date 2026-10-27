#pragma once

#include "particle_menu.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ow::ui::particle
{
class ParticleMenuSessions
{
public:
	bool Open(std::uint64_t steamId, bool steamAuthenticated,
			  std::vector<std::array<MenuItem, ParticleMenu::ItemsPerPage>> pages, float yaw, float pitch);
	bool Close(std::uint64_t steamId);
	bool UpdateView(std::uint64_t steamId, float yaw, float pitch);
	std::optional<MenuSelection> HandleActivateInput(std::uint64_t steamId, bool pressed);
	ParticleMenu* Find(std::uint64_t steamId);
	const ParticleMenu* Find(std::uint64_t steamId) const;
	void Clear();

private:
	struct Session
	{
		ParticleMenu menu;
		float lastYaw {};
		float lastPitch {};
		bool activateButtonDown {};
	};

	std::unordered_map<std::uint64_t, Session> sessions;
};
} // namespace ow::ui::particle
