#pragma once

#include "arena.hpp"
#include "seqlock.hpp"
#include "index.hpp"
#include <cstring>
#include <string>
#include <utility>

// Hard limits for Version 1 (simplifies alignment logic).
constexpr std::size_t MAX_KEY = 255;
constexpr std::size_t MAX_VAL = 65535;

enum class Status { OK, KeyTooLong, ValTooLong, ArenaFull, NotFound };

/// \brief On-disk/In-Arena Header.
/// \details Packed immediately before the Key and Value bytes.
struct alignas(8) EntryHeader {
    std::uint16_t klen;
    std::uint16_t vlen;
    std::uint32_t hash;
};

/// \brief Hyperion Storage Engine.
/// \details Orchestrates the Arena (Storage), Index (Lookup), and SeqLock (Concurrency).
class Hyperion {
public:
    // Default constructor (Invalid state for RVO fallback).
    Hyperion() = default;

    /// \brief Factory method for creating the DB instance.
    /// \details Uses RVO (Return Value Optimization) to construct the Move-Only members in-place.
    static Hyperion create(std::size_t bytes, std::uint32_t slots, ArenaError& ae) {
        Arena a = Arena::create(bytes, ae);
        if (ae != ArenaError::None) {
            return Hyperion();
        }
        
        Index idx; 
        idx.init(slots);
        
        // Move resources into the instance.
        return Hyperion(std::move(a), std::move(idx));
    }

    /// \brief Thread-safe Put (Single Writer).
    /// \details 
    /// 1. Computes hash.
    /// 2. Allocates aligned memory in Arena.
    /// 3. Writes Header + Key + Value.
    /// 4. Updates Index within a SeqLock Write transaction.
    Status put(const std::string& key, const std::string& val) {
        if (key.size() > MAX_KEY) return Status::KeyTooLong;
        if (val.size() > MAX_VAL) return Status::ValTooLong;

        std::uint32_t h = Index::hash((const std::uint8_t*)key.data(), key.size());
        std::uint8_t tag = static_cast<std::uint8_t>(h >> 24);

        // Calculate size aligned to 8 bytes to prevent unaligned access penalties.
        std::uint32_t needed = static_cast<std::uint32_t>(sizeof(EntryHeader) + key.size() + val.size());
        needed = (needed + 7) & ~7; 
        
        std::uint32_t offset;
        if (arena_.alloc(needed, offset) != ArenaError::None) return Status::ArenaFull;

        // Direct memory write (memcpy) to mapped region.
        auto* ptr = arena_.ptr_at(offset);
        auto* hdr = new (ptr) EntryHeader; // Placement new
        hdr->klen = static_cast<std::uint16_t>(key.size());
        hdr->vlen = static_cast<std::uint16_t>(val.size());
        hdr->hash = h;
        std::memcpy(ptr + sizeof(EntryHeader), key.data(), key.size());
        std::memcpy(ptr + sizeof(EntryHeader) + key.size(), val.data(), val.size());

        // Publish to Index (Critical Section).
        index_.write([&](Index& idx) {
            auto eq = [&](const Slot& s) {
                if (!s.is_valid()) return false;
                auto* e = (EntryHeader*)arena_.ptr_at(s.offset);
                // Verify full hash and length before memcmp to save cycles.
                if (e->hash != h || e->klen != key.size()) return false;
                return std::memcmp((std::uint8_t*)(e + 1), key.data(), key.size()) == 0;
            };

            auto [slot_idx, exists] = idx.find(h, key.size(), eq);
            // Append-only logic: Always point to the new offset. Old data remains as garbage.
            idx.update(slot_idx, tag, static_cast<std::uint8_t>(key.size()), static_cast<std::uint16_t>(val.size()), offset);
        });

        return Status::OK;
    }

    /// \brief Lock-free Get (Multi-Reader).
    /// \details Uses SeqLock optimistic reading. Retry loop handles concurrent writes.
    Status get(const std::string& key, std::string& out_val) const {
        std::uint32_t h = Index::hash((const std::uint8_t*)key.data(), key.size());
        
        bool found = index_.read([&](const Index& idx) {
            auto eq = [&](const Slot& s) {
                if (!s.is_valid()) return false;
                auto* e = (EntryHeader*)arena_.ptr_at(s.offset);
                if (e->hash != h || e->klen != key.size()) return false;
                return std::memcmp((std::uint8_t*)(e + 1), key.data(), key.size()) == 0;
            };

            auto [slot_idx, exists] = idx.find(h, key.size(), eq);
            if (exists) {
                const Slot& s = idx.at(slot_idx);
                auto* e = (EntryHeader*)arena_.ptr_at(s.offset);
                const char* vptr = (const char*)e + sizeof(EntryHeader) + e->klen;
                // Copy out to string. For true zero-copy, return a std::string_view (requires lifecycle management).
                out_val.assign(vptr, e->vlen);
                return true;
            }
            return false;
        });

        return found ? Status::OK : Status::NotFound;
    }

    /// \brief Logical Delete.
    /// \details Marks the index slot as a Tombstone. Does not reclaim Arena memory.
    Status del(const std::string& key) {
        std::uint32_t h = Index::hash((const std::uint8_t*)key.data(), key.size());
        bool found = false;
        
        index_.write([&](Index& idx) {
             auto eq = [&](const Slot& s) {
                if (!s.is_valid()) return false;
                auto* e = (EntryHeader*)arena_.ptr_at(s.offset);
                return (e->hash == h && e->klen == key.size() &&
                       std::memcmp((std::uint8_t*)(e + 1), key.data(), key.size()) == 0);
            };

            auto [slot_idx, exists] = idx.find(h, key.size(), eq);
            if (exists) {
                idx.at(slot_idx).make_tombstone();
                found = true;
            }
        });
        
        return found ? Status::OK : Status::NotFound;
    }

private:
    // Private Constructor prevents partial initialization.
    Hyperion(Arena&& a, Index&& idx) 
        : arena_(std::move(a)), index_(std::move(idx)) {}

    Arena arena_;
    SeqLock<Index> index_;
};