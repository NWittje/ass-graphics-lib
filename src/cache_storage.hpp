#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ag
{
// FIFO payload ownership stays explicit. Vacated slots release their references;
// capacity remains visible to the renderer's counted-CPU policy.
template <class T> class CacheQueue
{
    std::vector<T> slots;
    size_t head{}, count{};
    template <bool Constant> class Cursor
    {
        using Owner = std::conditional_t<Constant, const CacheQueue, CacheQueue>;
        Owner *owner{};
        size_t offset{};
        friend class CacheQueue;
        Cursor(Owner *o, size_t n) : owner(o), offset(n) {}
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Constant, const T &, T &>;
        using pointer = std::conditional_t<Constant, const T *, T *>;
        Cursor() = default;
        reference operator*() const { return owner->slots[(owner->head + offset) % owner->slots.size()]; }
        Cursor &operator++() { ++offset; return *this; }
        Cursor operator++(int) { auto previous = *this; ++*this; return previous; }
        bool operator==(const Cursor &r) const { return owner == r.owner && offset == r.offset; }
    };
public:
    using iterator = Cursor<false>;
    using const_iterator = Cursor<true>;
    size_t size() const { return count; }
    size_t capacity() const { return slots.capacity(); }
    bool empty() const { return !count; }
    iterator begin() { return {this, 0}; }
    iterator end() { return {this, count}; }
    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, count}; }
    T &front() { assert(count); return slots[head]; }
    const T &front() const { assert(count); return slots[head]; }
    void reserve(size_t n)
    {
        if (n <= slots.size()) return;
        std::vector<T> next(n);
        for (size_t i = 0; i < count; ++i) next[i] = std::move(slots[(head + i) % slots.size()]);
        slots.swap(next);
        head = 0;
    }
    void push_back(T value)
    {
        if (count == slots.size()) reserve(std::max<size_t>(8, count * 2));
        slots[(head + count) % slots.size()] = std::move(value);
        ++count;
    }
    void pop_front()
    {
        assert(count);
        slots[head] = T{};
        head = (head + 1) % slots.size();
        if (!--count) head = 0;
    }
    void clear() { while (count) pop_front(); }
};

// Snapshots share an immutable sorted weak index. A bounded insertion tail is
// merged in one contiguous allocation, so a live cache above 65536 entries does
// not cause a node-by-node rebuild on every insertion. No geometry is owned here.
template <class T> class WeakCacheIndex
{
    struct Entry { uint64_t hash; std::weak_ptr<T> value; };
    std::shared_ptr<const std::vector<Entry>> sorted;
    std::vector<Entry> tail;
    static constexpr size_t merge_batch = 256;
    void compact()
    {
        std::sort(tail.begin(), tail.end(), [](auto &a, auto &b) { return a.hash < b.hash; });
        size_t live = 0;
        if (sorted) for (auto &e : *sorted) live += !e.value.expired();
        for (auto &e : tail) live += !e.value.expired();
        auto next = std::make_shared<std::vector<Entry>>();
        next->reserve(live);
        size_t a = 0, b = 0;
        while ((sorted && a < sorted->size()) || b < tail.size())
        {
            const auto &e = b == tail.size() || (sorted && a < sorted->size() &&
                            (*sorted)[a].hash <= tail[b].hash) ? (*sorted)[a++] : tail[b++];
            if (!e.value.expired()) next->push_back(e);
        }
        sorted = std::move(next);
        tail.clear();
    }
public:
    size_t size() const { return (sorted ? sorted->size() : 0) + tail.size(); }
    size_t storage_bytes() const
    {
        return private_storage_bytes() + shared_storage_bytes();
    }
    size_t private_storage_bytes() const { return tail.capacity() * sizeof(Entry); }
    size_t shared_storage_bytes() const
    { return sorted ? sizeof(*sorted) + sorted->capacity() * sizeof(Entry) : 0; }
    const void *shared_storage_identity() const { return sorted.get(); }
    void emplace(uint64_t hash, const std::shared_ptr<T> &value)
    {
        tail.push_back({hash, value});
        if (tail.size() >= merge_batch) compact();
    }
    template <class Predicate> std::shared_ptr<T> find(uint64_t hash, Predicate same) const
    {
        for (auto i = tail.rbegin(); i != tail.rend(); ++i)
            if (i->hash == hash)
                if (auto value = i->value.lock(); value && same(*value))
                    return value;
        if (sorted)
        {
            auto i = std::lower_bound(sorted->begin(), sorted->end(), hash,
                                      [](auto &e, uint64_t key) { return e.hash < key; });
            for (; i != sorted->end() && i->hash == hash; ++i)
                if (auto value = i->value.lock(); value && same(*value))
                    return value;
        }
        return {};
    }
    void erase(uint64_t hash)
    {
        std::erase_if(tail, [=](auto &e) { return e.hash == hash; });
        if (sorted)
        {
            auto next = std::make_shared<std::vector<Entry>>();
            next->reserve(sorted->size());
            for (auto &e : *sorted) if (e.hash != hash) next->push_back(e);
            sorted = std::move(next);
        }
    }
    void clear() { sorted.reset(); tail.clear(); }
};

// Temporary exact pointer membership for payload accounting. Open addressing
// avoids one heap allocation per cached mesh without changing deduplication.
template <class Pointer> class PointerSet
{
    static_assert(std::is_pointer_v<Pointer>);
    std::vector<Pointer> slots;
    size_t count{}, occupied{};
    static Pointer deleted() { return reinterpret_cast<Pointer>(uintptr_t(1)); }
    static size_t hash(Pointer p)
    {
        uint64_t value = reinterpret_cast<uintptr_t>(p);
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdull;
        return size_t(value ^ (value >> 33));
    }
    void grow(size_t n)
    {
        std::vector<Pointer> next(n);
        for (auto p : slots) if (p && p != deleted())
        {
            auto i = hash(p) & (n - 1);
            while (next[i]) i = (i + 1) & (n - 1);
            next[i] = p;
        }
        slots.swap(next);
        occupied = count;
    }
public:
    struct Inserted { bool second; };
    void reserve(size_t entries)
    {
        size_t n = 16;
        while (n / 2 < entries) n *= 2;
        if (n > slots.size()) grow(n);
    }
    bool contains(Pointer p) const
    {
        if (!p || slots.empty()) return false;
        for (auto i = hash(p) & (slots.size() - 1); slots[i]; i = (i + 1) & (slots.size() - 1))
            if (slots[i] == p) return true;
        return false;
    }
    Inserted insert(Pointer p)
    {
        if (!p) return {false};
        if (slots.empty()) grow(16);
        if ((occupied + 1) * 4 >= slots.size() * 3)
            grow(count * 2 < slots.size() ? slots.size() : slots.size() * 2);
        size_t free = slots.size();
        auto i = hash(p) & (slots.size() - 1);
        for (; slots[i]; i = (i + 1) & (slots.size() - 1))
        {
            if (slots[i] == p) return {false};
            if (slots[i] == deleted() && free == slots.size()) free = i;
        }
        if (free != slots.size()) i = free; else ++occupied;
        slots[i] = p;
        ++count;
        return {true};
    }
    size_t erase(Pointer p)
    {
        if (!p || slots.empty()) return 0;
        for (auto i = hash(p) & (slots.size() - 1); slots[i]; i = (i + 1) & (slots.size() - 1))
            if (slots[i] == p) { slots[i] = deleted(); --count; return 1; }
        return 0;
    }
};
}
