#pragma once

#include <cstdint>
#include <vector>

/*
Transposition table.

Moves in this game commute constantly — most of the tree is permutations of the same
handful of quiet shuffles — so the same position is reached by an enormous number of
distinct move orders. Collapsing those is the single largest saving available to the
forward search.

The board word already identifies a position exactly, so there is no need for Zobrist
keys: hashing (board, side to move) with a bijective mixer gives a full-width key with
no collision risk beyond the truncation we choose ourselves.

The 50-move rule makes a position's value depend on the halfmove clock, so the clock is
stored and an entry is only reused at the same clock. That costs less than it sounds:
transpositions here are almost always reorderings of the same moves, which reach the
position with the identical clock.
*/
namespace tt
{

enum Bound : unsigned char
{
    BOUND_NONE = 0,
    BOUND_EXACT = 1,
    BOUND_LOWER = 2, // score is at least this (fail-high / beta cutoff)
    BOUND_UPPER = 3, // score is at most this (fail-low)
};

struct Entry
{
    unsigned long long key = 0;
    int score = 0;
    unsigned char depth = 0;
    unsigned char bound = BOUND_NONE;
    unsigned char halfmove = 0;
    unsigned char move = 0;
    bool indeterminate = false;
};

inline unsigned long long mix64(unsigned long long x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline unsigned long long positionKey(unsigned long long board, bool active)
{
    return mix64(board ^ (active ? 0xD6E8FEB86659FD93ULL : 0ULL));
}

class Table
{
public:
    explicit Table(unsigned int log2Entries = 22)
    {
        resize(log2Entries);
    }

    void resize(unsigned int log2Entries)
    {
        entries.assign(std::size_t{1} << log2Entries, Entry{});
        mask = entries.size() - 1;
    }

    void clear()
    {
        std::fill(entries.begin(), entries.end(), Entry{});
        hits = 0;
        stores = 0;
    }

    std::size_t size() const { return entries.size(); }
    unsigned long long hitCount() const { return hits; }
    unsigned long long storeCount() const { return stores; }

    const Entry *find(unsigned long long key, unsigned int halfmove) const
    {
        const Entry &e = entries[key & mask];
        if (e.bound != BOUND_NONE && e.key == key && e.halfmove == halfmove)
        {
            ++hits;
            return &e;
        }
        return nullptr;
    }

    void store(unsigned long long key,
               int score,
               unsigned int depth,
               Bound bound,
               unsigned int halfmove,
               unsigned char move,
               bool indeterminate)
    {
        Entry &e = entries[key & mask];

        // Depth-preferred replacement, but always overwrite a different position so a
        // stale deep entry cannot wedge a slot permanently.
        if (e.bound != BOUND_NONE && e.key == key && e.depth > depth)
        {
            return;
        }

        e.key = key;
        e.score = score;
        e.depth = static_cast<unsigned char>(depth);
        e.bound = static_cast<unsigned char>(bound);
        e.halfmove = static_cast<unsigned char>(halfmove);
        e.move = move;
        e.indeterminate = indeterminate;
        ++stores;
    }

private:
    std::vector<Entry> entries;
    std::size_t mask = 0;
    mutable unsigned long long hits = 0;
    unsigned long long stores = 0;
};

} // namespace tt
