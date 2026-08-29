#pragma once

// In-memory content-addressed cache (spec §5.3, §12.6): an LRU over
// fingerprint -> TypedValue payload, shared across runs of the same host.
// Keys come from BindingFingerprinter (structure + launch params + rng
// identities + numeric profile), so editing the tail of a file only
// invalidates what is downstream of the edit (N4) and a profile change
// invalidates everything (N3).
//
// Only value bindings are stored (geometry, scalars, strings, lists): field
// and rng bindings recompile for pennies and never enter the cache. The
// instance is owned by the caller and passed through RunParams::cache
// (nullptr disables caching); the engine itself is single-threaded across
// runs, the mutex guards concurrent hosts sharing one cache.
//
// By design inspector/debug sessions (stage E6) get their own cache instance:
// enabling debug must not evict production entries (spec §5.3).
// The persistent disk layer with the same key is deferred (Q5, spec §12.6).

#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

#include "field.h"

namespace pgg {

class MemoryCache {
public:
    explicit MemoryCache(size_t capacity = 512) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Copy on hit (the stored payload is immutable); the entry becomes MRU.
    bool lookup(uint64_t key, TypedValue& out);
    // Insert or refresh; evicts the LRU entry when over capacity.
    void store(uint64_t key, const TypedValue& value);

    size_t size() const;
    size_t capacity() const { return capacity_; }

private:
    using LruList = std::list<uint64_t>;  // front = most recently used
    struct Entry {
        TypedValue value;
        LruList::iterator lru;
    };

    size_t capacity_;
    mutable std::mutex m_;
    LruList lru_;
    std::unordered_map<uint64_t, Entry> map_;
};

}  // namespace pgg
