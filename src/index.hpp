#pragma once

#include <cstdint>
#include <memory>
#include <cstring>
#include <algorithm>

/// \brief Fixed-size Index Slot.
/// \details
/// 16-byte structure aligned to 16 bytes. This enables potential SIMD optimizations
/// (loading 4 slots into a 64-byte cache line or AVX-512 register).
struct alignas(16) Slot {
    std::uint8_t  hash_tag;   // High 8 bits of hash for cheap comparisons
    std::uint8_t  key_len;    // Fast rejection filter
    std::uint16_t val_len;    // Data size metadata
    std::uint32_t offset;     // Offset into Arena (0 = Invalid)
    std::uint64_t _padding;   // Padding to force 16-byte alignment

    static constexpr std::uint32_t OFF_EMPTY = 0xFFFFFFFF;
    static constexpr std::uint32_t OFF_TOMB = 0xFFFFFFFE;

    static Slot empty() { return {0, 0, 0, OFF_EMPTY, 0}; }
    
    bool is_empty() const { return offset == OFF_EMPTY; }
    bool is_tombstone() const { return offset == OFF_TOMB; }
    bool is_valid() const { return offset < OFF_TOMB; }
    
    void make_tombstone() { offset = OFF_TOMB; hash_tag = 0; }
};

/// \brief Open-Addressing Hash Index with Linear Probing.
class Index {
public:
    Index() = default;
    
    // Move-only resource management
    Index(Index&&) = default;
    Index& operator=(Index&&) = default;
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    void init(std::uint32_t slots) {
        // Enforce Power-of-Two capacity for bitwise masking (faster than modulo).
        capacity_ = next_pow2(std::max(slots, 8u));
        mask_ = capacity_ - 1;
        slots_ = std::make_unique<Slot[]>(capacity_);
        for(std::uint32_t i=0; i<capacity_; ++i) slots_[i] = Slot::empty();
    }

    /// \brief FNV-1a Hash Implementation (32-bit).
    static std::uint32_t hash(const std::uint8_t* data, std::size_t len) {
        std::uint32_t h = 2166136261u;
        for(std::size_t i=0; i<len; ++i) {
            h ^= data[i];
            h *= 16777619u;
        }
        return h;
    }

    /// \brief Linear Probe Lookup.
    /// \param eq Functor for deep key comparison.
    /// \return Pair {Index, Found}. If !Found, Index is the insertion candidate.
    template <typename KeyEq>
    std::pair<std::uint32_t, bool> find(std::uint32_t h, std::size_t klen, KeyEq&& eq) const {
        std::uint8_t tag = static_cast<std::uint8_t>(h >> 24);
        std::uint32_t idx = h & mask_;
        std::uint32_t first_tomb = UINT32_MAX;

        // Bounded probe loop. In production, max probe length should be monitored.
        for(std::uint32_t i=0; i<capacity_; ++i) {
            const Slot& s = slots_[idx];
            
            if (s.is_empty()) {
                // Return first recycled tombstone if available, else current empty slot.
                return { (first_tomb != UINT32_MAX) ? first_tomb : idx, false };
            }
            
            if (s.is_tombstone()) {
                // Record first tombstone for recycling (Backshift/Tombstone optimization).
                if (first_tomb == UINT32_MAX) first_tomb = idx;
            } 
            else if (s.hash_tag == tag && s.key_len == klen) {
                // Tag match -> Invoke deep comparison.
                if (eq(s)) return {idx, true};
            }
            
            idx = (idx + 1) & mask_;
        }
        // Table Full fallback.
        return { (first_tomb != UINT32_MAX) ? first_tomb : idx, false };
    }

    void update(std::uint32_t idx, std::uint8_t tag, std::uint8_t klen, std::uint16_t vlen, std::uint32_t off) {
        Slot s;
        s.hash_tag = tag;
        s.key_len = klen;
        s.val_len = vlen;
        s.offset = off;
        s._padding = 0;
        slots_[idx] = s;
    }
    
    Slot& at(std::uint32_t idx) { return slots_[idx]; }
    const Slot& at(std::uint32_t idx) const { return slots_[idx]; }
    std::uint32_t cap() const { return capacity_; }
    std::uint32_t mask() const { return mask_; }

private:
    static std::uint32_t next_pow2(std::uint32_t v) {
        v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; return v + 1;
    }
    
    std::unique_ptr<Slot[]> slots_;
    std::uint32_t capacity_ = 0;
    std::uint32_t mask_ = 0;
};