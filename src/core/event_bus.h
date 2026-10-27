#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ow::core
{
class EventBus
{
public:
	using SubscriptionId = std::uint64_t;

	template <typename Event>
	SubscriptionId Subscribe(std::function<void(const Event&)> listener)
	{
		if (!listener)
		{
			return 0;
		}

		const SubscriptionId id = ++nextId;
		auto& listeners = subscriptions[std::type_index(typeid(Event))];
		listeners.push_back({id, [callback = std::move(listener)](const void* event) { callback(*static_cast<const Event*>(event)); }});
		return id;
	}

	bool Unsubscribe(SubscriptionId id)
	{
		if (id == 0)
		{
			return false;
		}
		for (auto& entry : subscriptions)
		{
			auto& listeners = entry.second;
			const auto oldSize = listeners.size();
			listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
										   [id](const Listener& listener) { return listener.id == id; }),
							listeners.end());
			if (listeners.size() != oldSize)
			{
				return true;
			}
		}
		return false;
	}

	template <typename Event>
	void Publish(const Event& event)
	{
		const auto found = subscriptions.find(std::type_index(typeid(Event)));
		if (found == subscriptions.end())
		{
			return;
		}

		const auto snapshot = found->second;
		for (const Listener& listener : snapshot)
		{
			if (IsSubscribed(listener.id))
			{
				listener.invoke(&event);
			}
		}
	}

private:
	struct Listener
	{
		SubscriptionId id;
		std::function<void(const void*)> invoke;
	};

	bool IsSubscribed(SubscriptionId id) const
	{
		for (const auto& entry : subscriptions)
		{
			if (std::any_of(entry.second.begin(), entry.second.end(),
							[id](const Listener& listener) { return listener.id == id; }))
			{
				return true;
			}
		}
		return false;
	}

	SubscriptionId nextId {};
	std::unordered_map<std::type_index, std::vector<Listener>> subscriptions;
};
} // namespace ow::core
