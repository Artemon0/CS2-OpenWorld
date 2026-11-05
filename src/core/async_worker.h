#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace ow::core
{
// Single background thread that executes jobs queued by the game thread and hands results back
// through TryTakeResult(). The plugin is built with exceptions disabled (-fno-exceptions /
// _HAS_EXCEPTIONS=0), so the worker is fail-closed instead of throwing: an invalid configuration
// (no handler or a zero queue capacity) leaves the object stopped, IsRunning() reports it, and
// Submit() rejects jobs. That keeps a misconfiguration visible without aborting the server.
//
// JobType must expose `requestId` and `steamId`; ResultType must expose the same two fields so the
// worker can stamp the correlation ids before the handler runs.
template <typename JobType, typename ResultType>
class AsyncWorker
{
public:
	using Handler = std::function<void(const JobType&, ResultType&)>;

	explicit AsyncWorker(Handler handler, std::size_t maxOutstanding) noexcept
		: handler(std::move(handler)), maxOutstanding(maxOutstanding)
	{
		if (!this->handler || maxOutstanding == 0)
		{
			stopping = true;
			return;
		}
		running = true;
		worker = std::thread(&AsyncWorker::WorkerLoop, this);
	}

	~AsyncWorker() { Stop(); }

	AsyncWorker(const AsyncWorker&) = delete;
	AsyncWorker& operator=(const AsyncWorker&) = delete;

	// False when the worker was constructed with an unusable configuration.
	bool IsRunning() const noexcept
	{
		std::lock_guard lock(mutex);
		return running;
	}

	bool Submit(JobType job)
	{
		{
			std::lock_guard lock(mutex);
			if (stopping || outstanding >= maxOutstanding)
			{
				return false;
			}
			jobs.push_back(std::move(job));
			++outstanding;
		}
		workAvailable.notify_one();
		return true;
	}

	std::optional<ResultType> TryTakeResult()
	{
		std::lock_guard lock(mutex);
		if (results.empty())
		{
			return std::nullopt;
		}
		ResultType result = std::move(results.front());
		results.pop_front();
		if (outstanding > 0)
		{
			--outstanding;
		}
		return result;
	}

	std::size_t Outstanding() const noexcept
	{
		std::lock_guard lock(mutex);
		return outstanding;
	}

	// Idempotent and thread-safe: the first call stops the queue, waits for the in-flight job to
	// finish and joins the thread; later calls do nothing.
	void Stop() noexcept
	{
		std::thread ownedWorker;
		{
			std::lock_guard lifecycle(lifecycleMutex);
			if (joined)
			{
				return;
			}
			joined = true;
			{
				std::lock_guard lock(mutex);
				stopping = true;
				running = false;
				outstanding -= jobs.size();
				jobs.clear();
				ownedWorker = std::move(worker);
			}
		}
		workAvailable.notify_all();
		if (ownedWorker.joinable())
		{
			ownedWorker.join();
		}
		std::lock_guard lock(mutex);
		results.clear();
		outstanding = 0;
	}

private:
	void WorkerLoop()
	{
		while (true)
		{
			JobType job;
			{
				std::unique_lock lock(mutex);
				workAvailable.wait(lock, [this] { return stopping || !jobs.empty(); });
				if (stopping)
				{
					return;
				}
				job = std::move(jobs.front());
				jobs.pop_front();
			}

			ResultType result;
			result.requestId = job.requestId;
			result.steamId = job.steamId;
			handler(job, result);

			{
				std::lock_guard lock(mutex);
				if (stopping)
				{
					if (outstanding > 0)
					{
						--outstanding;
					}
					continue;
				}
				results.push_back(std::move(result));
			}
		}
	}

	Handler handler;
	const std::size_t maxOutstanding;
	mutable std::mutex mutex;
	std::mutex lifecycleMutex;
	std::condition_variable workAvailable;
	std::deque<JobType> jobs;
	std::deque<ResultType> results;
	std::thread worker;
	std::size_t outstanding {};
	bool stopping {};
	bool running {};
	bool joined {};
};
} // namespace ow::core
