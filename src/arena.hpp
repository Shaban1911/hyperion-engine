#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>

// Platform Abstraction Layer (PAL) for Virtual Memory Management
#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

enum class ArenaError { None, OutOfSpace, MmapFailed, TooLarge };

/// \brief Monotonic Bump Allocator backed by a contiguous OS memory mapping.
///
/// \details
/// Bypasses the user-space heap (malloc/free) to eliminate fragmentation and metadata overhead.
/// Allocations are O(1) via an atomic fetch_add. Deallocation is impossible until destruction.
class Arena {
public:
    Arena() : base_(nullptr), size_(0), offset_(0) {}

    /// \brief Maps a contiguous region of virtual memory.
    static Arena create(std::size_t size_bytes, ArenaError& err) {
        Arena a;
        err = ArenaError::None;
        if (size_bytes > UINT32_MAX) { err = ArenaError::TooLarge; return a; }

        void* ptr = nullptr;

        #if defined(_WIN32)
            // MEM_RESERVE | MEM_COMMIT guarantees zero-initialized pages on demand.
            ptr = VirtualAlloc(nullptr, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (ptr == nullptr) { err = ArenaError::MmapFailed; return a; }
        #else
            // MAP_ANONYMOUS requests zero-filled pages from the kernel.
            ptr = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) { err = ArenaError::MmapFailed; return a; }
        #endif

        a.base_ = static_cast<std::uint8_t*>(ptr);
        a.size_ = static_cast<std::uint32_t>(size_bytes);
        
        // Reserve offset 0-7 to ensure no valid pointer has offset 0 (null-equivalent).
        // Alignment ensures 64-bit aligned data starts at offset 8.
        a.offset_.store(8, std::memory_order_relaxed); 
        return a;
    }

    ~Arena() {
        if (base_) {
            #if defined(_WIN32)
                VirtualFree(base_, 0, MEM_RELEASE);
            #else
                ::munmap(base_, size_);
            #endif
        }
    }

    // Move-only semantics to manage the OS handle ownership.
    Arena(Arena&& o) noexcept : base_(o.base_), size_(o.size_), offset_(o.offset_.load()) {
        o.base_ = nullptr; o.size_ = 0;
    }
    Arena& operator=(Arena&& o) = delete;
    Arena(const Arena&) = delete;

    /// \brief Thread-safe bump allocation.
    /// \return Offset relative to base address.
    inline ArenaError alloc(std::uint32_t size, std::uint32_t& out_offset) {
        // atomic fetch_add is a single CPU instruction on x64/ARMv8.
        std::uint32_t old_off = offset_.fetch_add(size, std::memory_order_acq_rel);
        
        if (old_off + size > size_) {
            return ArenaError::OutOfSpace;
        }
        out_offset = old_off;
        return ArenaError::None;
    }

    /// \brief Resolves an offset to a raw pointer.
    /// \note No bounds check in release builds for performance.
    inline std::uint8_t* ptr_at(std::uint32_t offset) const {
        return base_ + offset;
    }

private:
    std::uint8_t* base_;
    std::uint32_t size_;
    // Cache-line alignment of this atomic is implicit in class layout, 
    // but contention is low in single-writer scenarios.
    std::atomic<std::uint32_t> offset_;
};