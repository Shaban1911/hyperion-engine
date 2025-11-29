# Hyperion

A deterministic, high-frequency key-value store engineered for sub-microsecond latency.

Hyperion is a header-only C++20 library implementing an append-only Arena allocator and optimistic concurrency control (SeqLock). It bypasses standard kernel memory management to guarantee O(1) write latencies and cache-coherent read paths.

## Performance Benchmarks

**Environment:** Windows 11 (MSVC 19.44), Ryzen 9 (Generic)  
**Dataset:** 1,000,000 operations, 64-byte payload

| Operation | Implementation | Latency (Avg) | Speedup |
| :--- | :--- | :--- | :--- |
| **Insert** | `std::unordered_map` | 220.65 ns | 1.0x |
| | **Hyperion** | **89.06 ns** | **2.5x** |
| **Read** | `std::unordered_map` | 108.55 ns | 1.0x |
| | **Hyperion** | **75.83 ns** | **1.4x** |

## Architecture

Hyperion prioritizes instruction cache locality and zero-syscall hot paths over memory efficiency.

### 1. Memory Model (The Arena)

- **Allocator:** Monotonic bump pointer over a pre-allocated contiguous virtual memory region (`mmap` on Linux, `VirtualAlloc` on Windows).
- **Layout:** Data is packed sequentially. No linked lists. No pointer chasing. This minimizes TLB misses and ensures prefetcher efficiency.
- **Lifecycle:** Memory is never freed during runtime. Deletion marks a tombstone. Space is reclaimed only upon process restart.

### 2. Concurrency (The SeqLock)

- **Write:** Single-writer serialization (external). Writes use `release` semantics to publish data before updating the version counter.
- **Read:** Wait-free, optimistic multi-reader access. Readers spin on version mismatches using hardware-specific pause instructions (`_mm_pause` / `yield`) to reduce bus contention.
- **Safety:** Explicit `atomic_thread_fence(acquire)` prevents instruction sinking on weak memory models (ARM/POWER).

### 3. Indexing

- **Algorithm:** Linear Probing with Tombstone Recycling.
- **Density:** 16-byte aligned slots allow for potential SIMD metadata scanning.
- **Collision:** High-load degradation is mitigated by enforcing a strict load factor or over-provisioning the index (typical in HFT environments).

## Integration

Hyperion is header-only. Include the `src` directory in your include path.

```cpp
#include "hyperion.hpp"

// 1. Initialize (64MB Arena, 1024 Slots)
ArenaError ae;
auto db = Hyperion::create(64 * 1024 * 1024, 1024, ae);

// 2. Write (Single Thread Only)
db.put("ticker:AAPL", "price:150.00");

// 3. Read (Thread Safe, Wait-Free)
std::string val;
if (db.get("ticker:AAPL", val) == Status::OK) {
    // fast path
}
```

## Constraints

- **Fixed Capacity:** The Arena size is immutable after initialization to prevent latency spikes associated with OS page faults or resizing.
- **Single Writer:** The engine assumes a single logical writer thread. Multiple writers must be serialized via an external sequencer or spinlock.
- **No Defragmentation:** Deleted keys leak storage space until the process terminates. This design choice favors deterministic latency over memory conservation.

## Build & Test

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
./Release/hyperion_bench
```
