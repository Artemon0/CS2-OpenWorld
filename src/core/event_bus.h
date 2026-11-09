#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
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
		static constexpr std::size_t MaxPublishDepth = 64;
		static constexpr std::size_t MaxPendingEvents = 1024;

		// A queued callback captures `this`, so a bus must never be copied or moved.
		EventBus() = default;
		EventBus(const EventBus&) = delete;
		EventBus& operator=(const EventBus&) = delete;
		EventBus(EventBus&&) = delete;
		EventBus& operator=(EventBus&&) = delete;

		template<typename Event>
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
				listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [id](const Listener& listener) { return listener.id == id; }),
								listeners.end());
				if (listeners.size() != oldSize)
				{
					return true;
				}
			}
			return false;
		}

		// Synchronous delivery. Listeners that publish events from inside a listener (directly or
		// through a chain of subscribes) are cut off after MaxPublishDepth frames so a cycle cannot
		// exhaust the stack; the rejected publish is counted.
		template<typename Event>
		bool Publish(const Event& event)
		{
			if (publishDepth >= MaxPublishDepth)
			{
				++rejectedPublishes;
				return false;
			}
			const auto found = subscriptions.find(std::type_index(typeid(Event)));
			if (found == subscriptions.end())
			{
				return true;
			}

			++publishDepth;
			const auto snapshot = found->second;
			for (const Listener& listener : snapshot)
			{
				if (IsSubscribed(listener.id))
				{
					listener.invoke(&event);
				}
			}
			--publishDepth;
			return true;
		}

		// Owned copy of the event, delivered by DispatchPendingEvents() on the caller's thread.
		// Use this from listeners that would otherwise publish recursively.
		template<typename Event>
		bool Enqueue(Event event)
		{
			if (pending.size() >= MaxPendingEvents)
			{
				++rejectedPublishes;
				return false;
			}
			pending.emplace_back([this, event = std::move(event)] { Publish(event); });
			return true;
		}

		// Delivers at most maxEvents queued events. Listeners queued while dispatching run on the
		// next call, so a queue that keeps refilling itself is bounded per frame. Nested calls are
		// rejected because the outer dispatch owns the queue.
		std::size_t DispatchPendingEvents(std::size_t maxEvents = 256)
		{
			if (dispatching)
			{
				return 0;
			}
			dispatching = true;
			const std::size_t count = (std::min)(maxEvents, pending.size());
			std::size_t delivered = 0;
			while (delivered < count && !pending.empty())
			{
				// Move the callback out first: it may queue more events into pending.
				auto invoke = std::move(pending.front());
				pending.pop_front();
				invoke();
				++delivered;
			}
			dispatching = false;
			return delivered;
		}

		std::size_t PendingEventCount() const noexcept
		{
			return pending.size();
		}

		std::size_t RejectedPublishCount() const noexcept
		{
			return rejectedPublishes;
		}

		// Drops queued events, listeners and counters on plugin unload.
		void Clear()
		{
			pending.clear();
			subscriptions.clear();
			rejectedPublishes = 0;
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
				if (std::any_of(entry.second.begin(), entry.second.end(), [id](const Listener& listener) { return listener.id == id; }))
				{
					return true;
				}
			}
			return false;
		}

		SubscriptionId nextId {};
		std::size_t publishDepth {};
		std::size_t rejectedPublishes {};
		bool dispatching {};
		std::deque<std::function<void()>> pending;
		std::unordered_map<std::type_index, std::vector<Listener>> subscriptions;
	};
} // namespace ow::core
