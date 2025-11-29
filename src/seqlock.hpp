#pragma once

#include <atomic>
#include <cassert>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
    #include <intrin.h> 
#elif defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h> 
#endif

/// \brief A Single-Writer / Multi-Reader Optimistic Lock.
/// 
/// \details
/// Implements the Seqlock pattern (Sequence Lock). Readers speculate that
/// no write occurs during their read critical section. If the sequence number
/// changes (or is odd), the read is retried.
///
/// \tparam T The data protected by the lock. Must be naturally aligned if accessed directly.
template <typename T>
class SeqLock {
public:
    SeqLock() : seq_(0), data_{} {}
    
    // Support Move Construction for owning types (e.g., unique_ptr handles)
    explicit SeqLock(T&& initial) : seq_(0), data_(std::move(initial)) {}
    explicit SeqLock(const T& initial) : seq_(0), data_(initial) {}

    SeqLock(const SeqLock&) = delete;
    SeqLock& operator=(const SeqLock&) = delete;

    /// \brief Optimistic read transaction.
    /// \param f Lambda/Function taking (const T&)
    /// \return The result of f(data_)
    /// 
    /// \note This function may loop indefinitely if write contention is extremely high.
    /// It is wait-free for the writer, but not for readers.
    template <typename F>
    auto read(F&& f) const -> decltype(f(std::declval<const T&>())) {
        for (;;) {
            // Load Version (Acquire): Ensures we see latest updates before speculative read.
            std::uint64_t v1 = seq_.load(std::memory_order_acquire);
            
            // If odd, a write is in progress. Spin-wait to reduce bus contention.
            if (v1 & 1) {
                #if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
                    _mm_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield");
                #endif
                continue;
            }

            // Speculative Read Critical Section.
            auto result = f(data_);

            // LoadLoad Fence: Prevents the CPU/Compiler from reordering the 'seq_' check (v2)
            // before the data read 'f(data_)'. Essential on weak memory models (ARM/POWER).
            std::atomic_thread_fence(std::memory_order_acquire);

            // Validate consistency.
            std::uint64_t v2 = seq_.load(std::memory_order_relaxed);
            if (v1 == v2) {
                return result;
            }
        }
    }

    /// \brief Exclusive write transaction.
    /// \details Only one writer thread is allowed at a time. This is asserted, not enforced.
    template <typename F>
    void write(F&& f) {
        // Increment to odd (Acquire): Signals "Write Pending". 
        // Establishes a happens-before relationship with previous readers.
        std::uint64_t prev = seq_.fetch_add(1, std::memory_order_acquire);
        
        // Hard invariant check: This primitive does not handle write-side contention.
        assert((prev & 1) == 0 && "Concurrent writers detected! SeqLock requires external write serialization.");

        // Mutation.
        f(data_);

        // Increment to even (Release): Signals "Write Complete".
        // Ensures all data mutations are visible before the sequence updates.
        seq_.store(prev + 2, std::memory_order_release);
    }

private:
    std::atomic<std::uint64_t> seq_;
    T data_;
};