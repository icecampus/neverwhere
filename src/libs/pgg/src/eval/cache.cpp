#include "../../pch.h"

#include "cache.h"

namespace pgg {

bool MemoryCache::lookup(uint64_t key, TypedValue& out) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    lru_.splice(lru_.begin(), lru_, it->second.lru);  // touch: now MRU
    out = it->second.value;
    return true;
}

void MemoryCache::store(uint64_t key, const TypedValue& value) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second.value = value;
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return;
    }
    lru_.push_front(key);
    Entry e{value, lru_.begin()};
    map_.emplace(key, std::move(e));
    while (map_.size() > capacity_) {
        const uint64_t evict = lru_.back();
        lru_.pop_back();
        map_.erase(evict);
    }
}

size_t MemoryCache::size() const {
    std::lock_guard<std::mutex> lk(m_);
    return map_.size();
}

}  // namespace pgg
