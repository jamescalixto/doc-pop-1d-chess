#pragma once

#include "retro/solver.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <unordered_map>

/*
Random access into a solved table file, without reading it in.

`Solver::load` pulls every slice onto the heap and rebuilds every combinatorial index up
front. For the whole game that is 2.20 GiB resident and a long wait before the first
question can be asked. That is the right trade for a verification pass, which visits every
position exactly once, and the wrong one for an interactive session, which visits almost
none of them.

This maps the file instead:

  - Opening walks the record headers only, seeking over each payload rather than reading
    it, so a 2.20 GiB table opens in milliseconds and costs one directory entry per slice
    (4,512 of them for the full game — a few hundred kilobytes).
  - A probe touches only the pages holding the position it asks about. The kernel keeps
    the hot ones and evicts the rest under pressure, so exploring a game settles at a few
    megabytes resident no matter how large the file is.
  - Slice indexes are built the first time a slice is touched and cached after that. A
    whole game touches a few dozen, and each build is pure combinatorics.

The payload layouts are exactly what `Solver::writeSlice` emits, so this reads the same
files the solver writes with no conversion step. Reads go through `memcpy` rather than a
cast, because a distance-to-mate payload is 4 bytes per placement and can leave the next
record header on an odd 8-byte boundary.
*/
namespace retro
{

class TableReader
{
public:
    ~TableReader() { close(); }

    TableReader() = default;
    TableReader(const TableReader &) = delete;
    TableReader &operator=(const TableReader &) = delete;

    /*
    Map `path` and index its slices. Returns false and explains itself on stderr if the
    file is missing, truncated to nothing, or was written by a build that disagrees.
    */
    bool open(const string &path)
    {
        close();

        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0)
        {
            std::fprintf(stderr, "cannot open %s\n", path.c_str());
            return false;
        }

        struct stat info{};
        if (::fstat(fd, &info) != 0 || info.st_size <= 0)
        {
            std::fprintf(stderr, "cannot size %s\n", path.c_str());
            ::close(fd);
            return false;
        }
        mappedBytesTotal = static_cast<uint64_t>(info.st_size);

        void *address = ::mmap(nullptr, mappedBytesTotal, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd); // the mapping keeps its own reference
        if (address == MAP_FAILED)
        {
            std::fprintf(stderr, "cannot map %s\n", path.c_str());
            mappedBytesTotal = 0;
            return false;
        }
        base = static_cast<const unsigned char *>(address);

        // Random single-position reads, so tell the kernel not to read ahead.
        ::madvise(const_cast<unsigned char *>(base), mappedBytesTotal, MADV_RANDOM);

        if (!readDirectory(path))
        {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (base)
        {
            ::munmap(const_cast<unsigned char *>(base), mappedBytesTotal);
            base = nullptr;
        }
        mappedBytesTotal = 0;
        directory.clear();
        sliceCache.clear();
    }

    bool isOpen() const { return base != nullptr; }
    Mode mode() const { return fileMode; }
    bool mirrored() const { return fileMirrored; }
    std::size_t sliceCount() const { return directory.size(); }
    std::size_t cachedSliceCount() const { return sliceCache.size(); }
    uint64_t mappedBytes() const { return mappedBytesTotal; }
    unsigned long long positionCount() const { return totalPositions; }

    bool tracksDistance() const { return fileMode == Mode::Dtm; }
    Value negate(Value v) const { return negateValue(v, tracksDistance()); }
    string describe(Value v) const { return describeValue(v, tracksDistance()); }

    /*
    Value of an arbitrary board. Mirroring is handled the way the solver handles it: a
    board whose slice is not the canonical one is reflected first, and because the
    reflection swaps the colours *and* the side to move, the value reads out unchanged.
    */
    Value lookup(unsigned long long board, bool whiteToMove, bool *found = nullptr) const
    {
        SliceKey key = sliceKeyOf(board);
        if (fileMirrored && !isCanonicalSlice(key))
        {
            board = mirrorBoard(board);
            whiteToMove = !whiteToMove;
            key = mirrorSliceKey(key);
        }

        const auto entry = directory.find(key.packed());
        if (entry == directory.end())
        {
            if (found) *found = false;
            return VALUE_UNRESOLVED;
        }

        const Slice *slice = sliceFor(key);
        if (!slice)
        {
            if (found) *found = false;
            return VALUE_UNRESOLVED;
        }

        const unsigned long long index = slice->encode(board);
        if (index == Slice::INVALID)
        {
            if (found) *found = false;
            return VALUE_UNRESOLVED;
        }

        if (found) *found = true;
        return valueAt(entry->second, index, whiteToMove);
    }

private:
    struct Record
    {
        uint64_t payloadOffset = 0;
        uint64_t payloadBytes = 0;
        uint64_t placements = 0;
    };

    // Mirrors of the private layout in Solver. Kept in step by FORMAT_VERSION.
    static constexpr char MAGIC[8] = {'1', 'D', 'C', 'H', 'E', 'S', 'S', '\0'};
    static constexpr uint32_t FORMAT_VERSION = 1;

    struct Header
    {
        char magic[8];
        uint32_t version;
        uint8_t mode;
        uint8_t mirror;
        uint16_t pad;
        uint64_t reserved;
    };

    struct RecordHeader
    {
        uint32_t packedKey;
        uint32_t pad;
        uint64_t placements;
        uint64_t payloadBytes;
    };

    /*
    Walk the records, keeping only where each payload lives. A record whose payload runs
    past the end of the file is where a killed run tore its checkpoint; everything before
    it is still good, so the scan stops there rather than failing.
    */
    bool readDirectory(const string &path)
    {
        if (mappedBytesTotal < sizeof(Header))
        {
            std::fprintf(stderr, "%s is too short to be a table file\n", path.c_str());
            return false;
        }

        Header header{};
        std::memcpy(&header, base, sizeof(header));
        if (std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 ||
            header.version != FORMAT_VERSION)
        {
            std::fprintf(stderr, "%s is not a table file this build understands\n", path.c_str());
            return false;
        }
        fileMode = (header.mode == 0) ? Mode::Wdl : (header.mode == 1) ? Mode::Dtm : Mode::Dtz;
        fileMirrored = header.mirror != 0;

        uint64_t offset = sizeof(Header);
        while (offset + sizeof(RecordHeader) <= mappedBytesTotal)
        {
            RecordHeader record{};
            std::memcpy(&record, base + offset, sizeof(record));
            offset += sizeof(RecordHeader);

            if (record.payloadBytes > mappedBytesTotal - offset)
            {
                break; // torn by a kill; keep everything before it
            }

            Record entry;
            entry.payloadOffset = offset;
            entry.payloadBytes = record.payloadBytes;
            entry.placements = record.placements;
            directory.emplace(record.packedKey, entry);
            totalPositions += 2 * record.placements;

            offset += record.payloadBytes;
        }

        if (directory.empty())
        {
            std::fprintf(stderr, "%s holds no complete slices\n", path.c_str());
            return false;
        }
        return true;
    }

    /*
    The combinatorial index for a slice, built on first use and kept. Building is pure
    arithmetic over the slice key, so this never touches the mapping.
    */
    const Slice *sliceFor(const SliceKey &key) const
    {
        const uint32_t packed = key.packed();
        const auto cached = sliceCache.find(packed);
        if (cached != sliceCache.end())
        {
            return &cached->second;
        }

        Slice slice;
        if (!slice.build(key))
        {
            return nullptr;
        }
        return &sliceCache.emplace(packed, std::move(slice)).first->second;
    }

    Value valueAt(const Record &record, unsigned long long index, bool whiteToMove) const
    {
        const unsigned long long slot = 2 * index + (whiteToMove ? 0 : 1);

        if (fileMode == Mode::Dtm)
        {
            const uint64_t at = record.payloadOffset + slot * sizeof(Value);
            if (at + sizeof(Value) > record.payloadOffset + record.payloadBytes)
            {
                return VALUE_UNRESOLVED;
            }
            Value value = VALUE_UNRESOLVED;
            std::memcpy(&value, base + at, sizeof(value));
            return value;
        }

        // Two bits per position, 32 to a 64-bit word, exactly as PackedWdl lays them out.
        const uint64_t wordIndex = slot >> 5;
        const uint64_t at = record.payloadOffset + wordIndex * sizeof(uint64_t);
        if (at + sizeof(uint64_t) > record.payloadOffset + record.payloadBytes)
        {
            return VALUE_UNRESOLVED;
        }
        uint64_t word = 0;
        std::memcpy(&word, base + at, sizeof(word));

        switch ((word >> ((slot & 31) * 2)) & 3)
        {
        case PackedWdl::CODE_WIN:
            return VALUE_WIN;
        case PackedWdl::CODE_LOSS:
            return VALUE_LOSS;
        case PackedWdl::CODE_ILLEGAL:
            return VALUE_ILLEGAL;
        default:
            return VALUE_DRAW;
        }
    }

    const unsigned char *base = nullptr;
    uint64_t mappedBytesTotal = 0;
    Mode fileMode = Mode::Wdl;
    bool fileMirrored = true;
    unsigned long long totalPositions = 0;

    std::unordered_map<uint32_t, Record> directory;
    mutable std::unordered_map<uint32_t, Slice> sliceCache;
};

} // namespace retro
