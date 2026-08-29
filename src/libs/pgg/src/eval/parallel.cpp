#include "../../pch.h"

#include "parallel.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace pgg {
namespace {

// True while this thread executes parallelFor chunk code — pool workers
// permanently, the calling thread for the duration of its dispatch. A nested
// parallelFor on such a thread must run inline: the pool has a single
// outstanding job, re-entering it would corrupt the running dispatch.
thread_local bool tInParallelChunk = false;

// Lazily created global pool, sized once from hardware_concurrency. The
// engine calls parallelFor from a single thread at a time, so there is at
// most one outstanding job; workers pick chunks off an atomic cursor and the
// caller participates, then waits for the chunk countdown to drain.
class ThreadPool {
public:
    static ThreadPool& instance() {
        static ThreadPool pool;
        return pool;
    }

    unsigned laneCount() const { return lanes_; }  // workers + the calling thread

    void run(size_t chunkCount, const std::function<void(size_t)>& fn) {
        {
            std::lock_guard<std::mutex> lk(m_);
            jobFn_ = &fn;
            jobChunks_ = chunkCount;
            next_.store(0, std::memory_order_relaxed);
            remaining_.store(chunkCount, std::memory_order_relaxed);
            ++generation_;
        }
        cv_.notify_all();
        drain();
        std::unique_lock<std::mutex> lk(doneM_);
        doneCv_.wait(lk, [&] { return remaining_.load(std::memory_order_acquire) == 0; });
    }

private:
    ThreadPool() {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        lanes_ = hw;
        for (unsigned i = 1; i < hw; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
            ++generation_;
        }
        cv_.notify_all();
        for (std::thread& t : workers_) t.join();
    }

    void workerLoop() {
        tInParallelChunk = true;
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || generation_ != seen; });
                if (stop_) return;
                seen = generation_;
            }
            drain();
        }
    }

    void drain() {
        for (;;) {
            const size_t c = next_.fetch_add(1, std::memory_order_relaxed);
            if (c >= jobChunks_) break;
            // The caller of run() cannot publish the next job while any chunk
            // of this one is unfinished (it waits on remaining_), so reading
            // jobFn_ here is race-free.
            (*jobFn_)(c);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(doneM_);
                doneCv_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    unsigned lanes_ = 1;
    std::mutex m_;
    std::condition_variable cv_;
    std::mutex doneM_;
    std::condition_variable doneCv_;
    const std::function<void(size_t)>* jobFn_ = nullptr;
    size_t jobChunks_ = 0;
    std::atomic<size_t> next_{0};
    std::atomic<size_t> remaining_{0};
    uint64_t generation_ = 0;
    bool stop_ = false;
};

}  // namespace

unsigned resolveThreadCount(unsigned threads) {
    if (threads > 0) return threads;
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

void parallelFor(size_t count, unsigned threads, const std::function<void(size_t, size_t)>& fn) {
    if (count == 0) return;
    const unsigned lanes = resolveThreadCount(threads);
    if (lanes <= 1 || count < kParallelThreshold || tInParallelChunk) {
        fn(0, count);
        return;
    }
    ThreadPool& pool = ThreadPool::instance();
    // ~4 chunks per lane for load balance; chunk size stays >= 1 element.
    size_t chunkCount = std::min<size_t>(count, static_cast<size_t>(std::min(lanes, pool.laneCount())) * 4);
    const size_t chunkSize = (count + chunkCount - 1) / chunkCount;
    chunkCount = (count + chunkSize - 1) / chunkSize;
    // The caller drains chunks too, so mark it for the whole dispatch: chunk
    // code calling parallelFor again must take the inline path here as well.
    tInParallelChunk = true;
    pool.run(chunkCount, [&](size_t c) {
        const size_t begin = c * chunkSize;
        fn(begin, std::min(count, begin + chunkSize));
    });
    tInParallelChunk = false;
}

}  // namespace pgg
