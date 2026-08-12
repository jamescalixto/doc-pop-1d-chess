#pragma once

#include "retro/slice.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <queue>
#include <unordered_map>
#include <vector>

/*
Retrograde solver over the slice DAG.

Values are exact win/draw/loss, computed by the classic backward induction:

    a position WINS  iff some legal move reaches a position that LOSES;
    a position LOSES iff it has legal moves and every one of them reaches a WIN;
    otherwise it DRAWS.

Nothing here is a heuristic and there is no horizon. The fixpoint is reached when no
further labels change, and everything still unlabelled is a draw — which is the right
answer, because a position that is neither a win nor a loss is exactly one that either
side can hold forever by repetition.

Slices are solved in `solveRank` order, so by the time a slice is reached every move that
leaves it lands in a slice already on the shelf. Within a slice, only quiet non-pawn
moves stay put, and those are the edges the backward pass walks.

TWO MODES, DELIBERATELY WRITTEN TWICE

`Mode::Wdl` is the production path: two bits per position, and a plain worklist, since
without distances there is nothing to order. It is what scales to the whole game.

`Mode::Dtm` additionally carries distance to mate in two bytes per position, which needs
a priority queue so that positions commit in ascending distance and the first value
proposed for a position is the true one.

The two share the slice machinery but their fixpoints are written out separately rather
than merged behind a flag. That is on purpose: they are independent implementations of
the same definition, so running both over the same material and comparing is a real
check rather than a tautology.

CAVEAT: this ignores the fifty-move rule, as tablebases conventionally do. Draws by
repetition are handled exactly. The fifty-move rule can only convert some of these wins
into draws; resolving that needs distance-to-zeroing-move rather than distance-to-mate.
*/
namespace retro
{

// Value of a position from the side to move's point of view.
//   0            draw
//   +(k + 1)     side to move mates in k plies
//   -(k + 1)     side to move is mated in k plies (so -1 means mated right now)
// In Wdl mode only the sign carries meaning: wins are +1 and losses -1.
using Value = int16_t;

inline constexpr Value VALUE_DRAW = 0;
inline constexpr Value VALUE_WIN = 1;
inline constexpr Value VALUE_LOSS = -1;
inline constexpr Value VALUE_UNRESOLVED = 32767;
inline constexpr Value VALUE_ILLEGAL = 32766;

enum class Mode
{
    Wdl,
    Dtm,
};

inline bool isResolved(Value v) { return v != VALUE_UNRESOLVED; }
inline bool isPlayable(Value v) { return v != VALUE_ILLEGAL && v != VALUE_UNRESOLVED; }

/*
The same position seen from the other side of the move that produced it: the sign flips
and, when distances are tracked, the distance grows by one ply.
*/
inline Value negateValue(Value v, bool trackDistance = true)
{
    if (v == VALUE_DRAW)
    {
        return VALUE_DRAW;
    }
    if (!trackDistance)
    {
        return (v > 0) ? VALUE_LOSS : VALUE_WIN;
    }
    return static_cast<Value>(-(v + (v > 0 ? 1 : -1)));
}

// Distance to mate, in plies. Only meaningful in Dtm mode.
inline int distanceToMate(Value v) { return (v > 0 ? v : -v) - 1; }

/*
Total order on values from the mover's point of view, so that "pick the best move" is a
plain maximum: win beats draw beats loss, a faster win beats a slower one, and a slower
loss beats a faster one.
*/
inline int valueOrder(Value v)
{
    if (v > 0)
    {
        return 1000000 - v;
    }
    if (v < 0)
    {
        return -1000000 - v;
    }
    return 0;
}

inline string describeValue(Value v, bool exactDistance = true)
{
    if (v == VALUE_DRAW)
    {
        return "draw";
    }
    if (v == VALUE_ILLEGAL)
    {
        return "illegal";
    }
    if (v == VALUE_UNRESOLVED)
    {
        return "unresolved";
    }
    if (!exactDistance)
    {
        return (v > 0) ? "win" : "loss";
    }
    return (v > 0 ? "win in " : "loss in ") + std::to_string(distanceToMate(v)) + " plies";
}

/*
Two bits per position: the whole game is 4.19 GiB this way, or 2.20 GiB once mirror
symmetry removes one slice of every pair.

Writes are read-modify-write on a shared word, so this is not safe to update from several
threads at once without partitioning by word.
*/
class PackedWdl
{
public:
    enum : uint8_t
    {
        CODE_DRAW = 0,
        CODE_WIN = 1,
        CODE_LOSS = 2,
        CODE_ILLEGAL = 3,
    };

    void assign(uint64_t count)
    {
        entries = count;
        words.assign((count + 31) / 32, 0);
    }

    uint64_t size() const { return entries; }
    uint64_t bytes() const { return words.size() * sizeof(uint64_t); }

    uint8_t get(uint64_t i) const
    {
        return static_cast<uint8_t>((words[i >> 5] >> ((i & 31) * 2)) & 3);
    }

    void set(uint64_t i, uint8_t code)
    {
        const unsigned int shift = (i & 31) * 2;
        uint64_t &word = words[i >> 5];
        word = (word & ~(uint64_t{3} << shift)) | (uint64_t{code} << shift);
    }

    // Raw access, for reading and writing the table to disk.
    const void *raw() const { return words.data(); }
    void *raw() { return words.data(); }

private:
    std::vector<uint64_t> words;
    uint64_t entries = 0;
};

/*
What a run is going to cost, worked out before any of it is spent.

Transient memory is the part worth checking: while a slice is being solved it needs a
label and a move counter per position, plus a worklist of everything that turned out to
be decisive. The worklist is sized here for the worst case where every position is
decisive; in practice about 40% are, so the measured peak comes in lower than this.
*/
struct SolvePlan
{
    unsigned long long slices = 0;
    unsigned long long placements = 0;
    unsigned long long positions = 0;
    uint64_t tableBytes = 0;
    SliceKey largestSlice;
    unsigned long long largestSlicePositions = 0;
    uint64_t largestTransientBytes = 0;

    // Tables stay resident for the whole run; only one slice's transients exist at a time.
    uint64_t projectedPeakBytes() const { return tableBytes + largestTransientBytes; }

    // 1 byte of label + 1 byte of counter + a 4-byte worklist slot, per position.
    static uint64_t transientBytesFor(unsigned long long positions)
    {
        return positions * (1 + 1 + sizeof(uint32_t));
    }
};

struct SolvedSlice
{
    Slice slice;
    PackedWdl wdl;            // Wdl mode
    std::vector<Value> dtm;   // Dtm mode; empty otherwise

    Value at(unsigned long long index, bool whiteToMove) const
    {
        const unsigned long long slot = 2 * index + (whiteToMove ? 0 : 1);
        if (!dtm.empty())
        {
            return dtm[slot];
        }
        switch (wdl.get(slot))
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

    uint64_t bytes() const
    {
        return dtm.empty() ? wdl.bytes() : dtm.size() * sizeof(Value);
    }
};

struct SolveStats
{
    unsigned long long slices = 0;
    unsigned long long positions = 0;
    unsigned long long illegal = 0;
    unsigned long long whiteWins = 0;
    unsigned long long blackWins = 0;
    unsigned long long draws = 0;
    unsigned long long encodeFailures = 0;
    uint64_t tableBytes = 0;
    uint64_t peakTransientBytes = 0;
};

class Solver
{
public:
    explicit Solver(Mode solveMode = Mode::Wdl, bool mirror = true)
        : mode(solveMode), useMirror(mirror)
    {
    }

    bool tracksDistance() const { return mode == Mode::Dtm; }
    bool mirroring() const { return useMirror; }
    Value negate(Value v) const { return negateValue(v, tracksDistance()); }
    string describe(Value v) const { return describeValue(v, tracksDistance()); }

    /*
    Which slices a run will cover and what it will cost, without solving anything. Slice
    construction is pure combinatorics, so this takes a fraction of a second even for the
    whole game — cheap enough to always run before committing to hours of compute.
    */
    SolvePlan plan(unsigned int maxPieces) const
    {
        SolvePlan result;
        Slice slice;
        for (const SliceKey &key : allSliceKeys())
        {
            if (key.pieceCount() > maxPieces || (useMirror && !isCanonicalSlice(key)))
            {
                continue;
            }
            if (!slice.build(key))
            {
                continue;
            }
            const unsigned long long positions = 2 * slice.size();
            result.slices += 1;
            result.placements += slice.size();
            result.positions += positions;
            result.tableBytes += (mode == Mode::Wdl) ? (positions + 31) / 32 * sizeof(uint64_t)
                                                     : positions * sizeof(Value);
            if (positions > result.largestSlicePositions)
            {
                result.largestSlicePositions = positions;
                result.largestSlice = key;
            }
        }
        result.largestTransientBytes = SolvePlan::transientBytesFor(result.largestSlicePositions);
        return result;
    }

    // How often to print a progress line during a long solve. Zero disables it.
    void setProgressInterval(double seconds) { progressInterval = seconds; }

    /*
    Append each slice to `path` as soon as it is solved, so a run that dies partway
    through can be resumed rather than repeated. Call before solve().

    A killed run can leave a half-written record at the end of the file. Loading stops
    cleanly at it, but appending after it would bury good data behind garbage that the
    next load would stop at again — so the partial record is cut off first and the file
    resumes from the last complete boundary.
    */
    bool openCheckpoint(const string &path)
    {
        uint64_t goodBytes = 0;
        switch (load(path, &goodBytes))
        {
        case LoadResult::Incompatible:
            // Never overwrite a table written by different settings.
            return false;

        case LoadResult::Loaded:
            if (::truncate(path.c_str(), static_cast<off_t>(goodBytes)) != 0)
            {
                std::fprintf(stderr, "could not trim %s to its last complete record\n",
                             path.c_str());
                return false;
            }
            checkpoint = std::fopen(path.c_str(), "ab");
            return checkpoint != nullptr;

        case LoadResult::Missing:
        default:
            checkpoint = std::fopen(path.c_str(), "wb");
            if (!checkpoint)
            {
                return false;
            }
            writeHeader(checkpoint);
            return true;
        }
    }

    void closeCheckpoint()
    {
        if (checkpoint)
        {
            std::fclose(checkpoint);
            checkpoint = nullptr;
        }
    }

    ~Solver() { closeCheckpoint(); }

    Solver(const Solver &) = delete;
    Solver &operator=(const Solver &) = delete;

    /*
    Solve every slice with at most `maxPieces` pieces on the board, kings included.
    */
    void solve(unsigned int maxPieces, bool verbose = true)
    {
        const SolvePlan expected = plan(maxPieces);
        const auto started = std::chrono::steady_clock::now();
        auto lastReport = started;
        unsigned long long done = 0, sliceIndex = 0;

        for (const SliceKey &key : allSliceKeys())
        {
            if (key.pieceCount() > maxPieces)
            {
                continue;
            }
            if (useMirror && !isCanonicalSlice(key))
            {
                continue; // its mirror carries the answer
            }
            if (solved.count(key.packed()))
            {
                continue; // already on the shelf, from a resumed checkpoint
            }

            solveSlice(key, verbose);

            // Many slice keys describe no legal placement at all and solve to nothing.
            // Those are not counted, so that progress matches what plan() predicted.
            const SolvedSlice *entry = find(key);
            if (!entry)
            {
                continue;
            }
            ++sliceIndex;
            done += 2 * entry->slice.size();

            if (progressInterval > 0 && !verbose)
            {
                const auto now = std::chrono::steady_clock::now();
                const double sinceReport = std::chrono::duration<double>(now - lastReport).count();
                if (sinceReport >= progressInterval || done >= expected.positions)
                {
                    lastReport = now;
                    reportProgress(std::chrono::duration<double>(now - started).count(), done,
                                   sliceIndex, expected);
                }
            }
        }
    }

    const SolvedSlice *find(const SliceKey &key) const
    {
        const auto it = solved.find(key.packed());
        return (it == solved.end()) ? nullptr : &it->second;
    }

    /*
    Value of an arbitrary board, if its slice has been solved. When mirroring, a board
    whose slice is not the canonical one is reflected first; the reflection swaps the
    colours *and* the side to move, so the value is read out unchanged.
    */
    Value lookup(unsigned long long board, bool whiteToMove, bool *found = nullptr) const
    {
        SliceKey key = sliceKeyOf(board);
        if (useMirror && !isCanonicalSlice(key))
        {
            board = mirrorBoard(board);
            whiteToMove = !whiteToMove;
            key = mirrorSliceKey(key);
        }

        const SolvedSlice *entry = find(key);
        if (!entry)
        {
            if (found)
            {
                *found = false;
            }
            return VALUE_UNRESOLVED;
        }
        const unsigned long long index = entry->slice.encode(board);
        if (index == Slice::INVALID)
        {
            if (found)
            {
                *found = false;
            }
            return VALUE_UNRESOLVED;
        }
        if (found)
        {
            *found = true;
        }
        return entry->at(index, whiteToMove);
    }

    const SolveStats &stats() const { return totals; }
    std::size_t sliceCount() const { return solved.size(); }

    /*
    Bytes actually held right now, counted from the tables themselves. The running stats
    only accumulate what this process solved, so on a resumed run they miss everything
    that came back off the disk.
    */
    uint64_t residentBytes() const
    {
        uint64_t total = 0;
        for (const auto &[packed, entry] : solved)
        {
            total += entry.bytes();
        }
        return total;
    }

    static string describeSlice(const SliceKey &key)
    {
        string name = "K";
        const struct
        {
            unsigned int bit;
            char letter;
        } whitePieces[] = {{MAT_WQ, 'Q'}, {MAT_WR, 'R'}, {MAT_WB, 'B'}, {MAT_WN, 'N'}, {MAT_WP, 'P'}};
        for (const auto &piece : whitePieces)
        {
            if (key.material & piece.bit)
            {
                name += piece.letter;
            }
        }
        name += "k";
        const struct
        {
            unsigned int bit;
            char letter;
        } blackPieces[] = {{MAT_BQ, 'q'}, {MAT_BR, 'r'}, {MAT_BB, 'b'}, {MAT_BN, 'n'}, {MAT_BP, 'p'}};
        for (const auto &piece : blackPieces)
        {
            if (key.material & piece.bit)
            {
                name += piece.letter;
            }
        }
        if (key.whitePawn != NO_SQUARE)
        {
            name += " P@" + std::to_string(key.whitePawn);
        }
        if (key.blackPawn != NO_SQUARE)
        {
            name += " p@" + std::to_string(key.blackPawn);
        }
        return name;
    }

    /*
    Visit every position inside this slice from which `board` is reachable by one quiet,
    non-pawn move — the only kind of move that does not leave the slice.

    Unmove generation is the mirror of move generation because every piece that can stay
    in a slice (king, knight, bishop, rook, queen) is a symmetric mover: if a piece
    standing on `to` attacks `from`, then the same piece standing on `from` attacks `to`,
    and the ray between them is equally clear either way. The origin must be empty, since
    a quiet move leaves nothing behind.
    */
    template <typename Visitor>
    static void forEachPredecessor(unsigned long long board,
                                   bool whiteToMove,
                                   const Slice &slice,
                                   Visitor &&visit,
                                   unsigned long long &encodeFailures)
    {
        const bool mover = !whiteToMove; // whoever moved into this position
        const unsigned int occupancy = getOccupancy(board);
        const unsigned int empty = ~occupancy & 0xFFFFu;

        for (unsigned int pieces = getPlayerOccupancy(board, mover); pieces; pieces &= pieces - 1)
        {
            const unsigned int to = attacks::lowestSquare(pieces);
            const unsigned int piece = getNthNibble(board, to);
            if (isPawn(piece))
            {
                continue; // a pawn move would have left the slice
            }

            for (unsigned int origins = attacks::pieceAttacks(piece, to, occupancy) & empty;
                 origins; origins &= origins - 1)
            {
                const unsigned int from = attacks::lowestSquare(origins);
                unsigned long long predecessor = blankNthNibble(board, to);
                predecessor = insertNthNibble(predecessor, piece, from);

                // The side that was not to move must not have been in check, or the
                // position could never have been on the board.
                if (isInCheck(predecessor, whiteToMove))
                {
                    continue;
                }

                const unsigned long long index = slice.encode(predecessor);
                if (index == Slice::INVALID)
                {
                    ++encodeFailures;
                    continue;
                }
                visit(index);
            }
        }
    }

private:
    // Labels used while a slice is being solved, before it is packed down.
    enum : uint8_t
    {
        STATE_UNRESOLVED = 0,
        STATE_DRAW = 1,
        STATE_WIN = 2,
        STATE_LOSS = 3,
        STATE_ILLEGAL = 4,
    };

    /*
    Where a move that leaves the slice lands. Resolved once per kind of move rather than
    once per move: the destination slice depends only on which piece was captured and how
    far a pawn stepped, so a 128-entry table covers every possibility a slice has.
    */
    struct SuccessorTarget
    {
        const SolvedSlice *slice = nullptr;
        bool mirrored = false;
        bool resolved = false;
    };

    Mode mode;
    bool useMirror;
    std::unordered_map<uint32_t, SolvedSlice> solved;
    SolveStats totals;
    double progressInterval = 5.0;
    std::FILE *checkpoint = nullptr;

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

    void writeHeader(std::FILE *file) const
    {
        Header header{};
        std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
        header.version = FORMAT_VERSION;
        header.mode = (mode == Mode::Wdl) ? 0 : 1;
        header.mirror = useMirror ? 1 : 0;
        std::fwrite(&header, sizeof(header), 1, file);
    }

    void writeSlice(std::FILE *file, const SliceKey &key, const SolvedSlice &entry) const
    {
        RecordHeader record{};
        record.packedKey = key.packed();
        record.placements = entry.slice.size();
        record.payloadBytes = entry.bytes();
        std::fwrite(&record, sizeof(record), 1, file);
        const void *payload = entry.dtm.empty() ? entry.wdl.raw()
                                                : static_cast<const void *>(entry.dtm.data());
        std::fwrite(payload, 1, record.payloadBytes, file);
    }

    void reportProgress(double elapsed,
                        unsigned long long done,
                        unsigned long long slicesDone,
                        const SolvePlan &expected) const
    {
        const double fraction =
            expected.positions ? static_cast<double>(done) / expected.positions : 1.0;
        const double rate = elapsed > 0 ? done / elapsed : 0;
        const double remaining = (rate > 0) ? (expected.positions - done) / rate : 0;
        std::printf("  %5.1f%%  %llu/%llu slices  %.2fB/%.2fB positions  %.1fM/s  "
                    "%s elapsed, %s left\n",
                    100.0 * fraction, slicesDone, expected.slices, done / 1e9,
                    expected.positions / 1e9, rate / 1e6, formatDuration(elapsed).c_str(),
                    formatDuration(remaining).c_str());
        std::fflush(stdout);
    }

public:
    static string formatDuration(double seconds)
    {
        char buffer[32];
        if (seconds < 90)
        {
            std::snprintf(buffer, sizeof(buffer), "%.0fs", seconds);
        }
        else if (seconds < 5400)
        {
            std::snprintf(buffer, sizeof(buffer), "%.0fm", seconds / 60);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.1fh", seconds / 3600);
        }
        return buffer;
    }

    /*
    Write every solved slice to `path` in one go.
    */
    bool save(const string &path) const
    {
        std::FILE *file = std::fopen(path.c_str(), "wb");
        if (!file)
        {
            return false;
        }
        writeHeader(file);
        for (const SliceKey &key : allSliceKeys())
        {
            const auto it = solved.find(key.packed());
            if (it != solved.end())
            {
                writeSlice(file, key, it->second);
            }
        }
        const bool ok = std::ferror(file) == 0;
        std::fclose(file);
        return ok;
    }

    /*
    Read tables back. Returns false if the file is absent or was written by a run with
    different settings — loading a mirrored table into an unmirrored solver, or a
    win/draw/loss table into a distance-to-mate one, would silently corrupt the answer.

    A record that is short or truncated ends the load without failing it: that is what a
    checkpoint from a run that was killed mid-write looks like, and everything before it
    is still good.
    */
    enum class LoadResult
    {
        Missing,      // no such file; a fresh run
        Incompatible, // exists but was written by different settings; do not touch it
        Loaded,       // records recovered
    };

    // Convenience for callers that only care whether anything came back.
    bool load(const string &path) { return load(path, nullptr) == LoadResult::Loaded; }

    LoadResult load(const string &path, uint64_t *goodBytes)
    {
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            return LoadResult::Missing;
        }

        Header header{};
        if (std::fread(&header, sizeof(header), 1, file) != 1 ||
            std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 ||
            header.version != FORMAT_VERSION)
        {
            std::fprintf(stderr, "%s is not a table file this build understands\n", path.c_str());
            std::fclose(file);
            return LoadResult::Incompatible;
        }
        const Mode fileMode = (header.mode == 0) ? Mode::Wdl : Mode::Dtm;
        if (fileMode != mode || (header.mirror != 0) != useMirror)
        {
            std::fprintf(stderr,
                         "%s holds %s tables solved %s mirroring; this solver wants %s, %s\n",
                         path.c_str(), header.mode == 0 ? "win/draw/loss" : "distance-to-mate",
                         header.mirror ? "with" : "without",
                         mode == Mode::Wdl ? "win/draw/loss" : "distance-to-mate",
                         useMirror ? "with mirroring" : "without mirroring");
            std::fclose(file);
            return LoadResult::Incompatible;
        }

        // Everything up to here is good, so an empty file resumes from just past its header.
        uint64_t complete = sizeof(Header);
        unsigned long long loaded = 0;
        for (;;)
        {
            RecordHeader record{};
            if (std::fread(&record, sizeof(record), 1, file) != 1)
            {
                break; // clean end, or a header torn by a kill
            }

            SliceKey key;
            key.material = record.packedKey & 0x3FF;
            key.whitePawn = static_cast<unsigned char>((record.packedKey >> 10) & 0x1F);
            key.blackPawn = static_cast<unsigned char>((record.packedKey >> 15) & 0x1F);

            SolvedSlice entry;
            if (!entry.slice.build(key) || entry.slice.size() != record.placements)
            {
                std::fprintf(stderr, "%s describes a slice this build does not agree with\n",
                             path.c_str());
                std::fclose(file);
                return LoadResult::Incompatible;
            }

            const unsigned long long positions = 2 * record.placements;
            if (mode == Mode::Wdl)
            {
                entry.wdl.assign(positions);
                if (entry.wdl.bytes() != record.payloadBytes ||
                    std::fread(entry.wdl.raw(), 1, record.payloadBytes, file) != record.payloadBytes)
                {
                    break; // torn payload; keep everything before it
                }
            }
            else
            {
                entry.dtm.assign(positions, VALUE_DRAW);
                if (entry.dtm.size() * sizeof(Value) != record.payloadBytes ||
                    std::fread(entry.dtm.data(), 1, record.payloadBytes, file) !=
                        record.payloadBytes)
                {
                    break;
                }
            }

            solved.emplace(record.packedKey, std::move(entry));
            complete = static_cast<uint64_t>(std::ftell(file));
            ++loaded;
        }

        std::fclose(file);
        if (goodBytes)
        {
            *goodBytes = complete;
        }
        if (loaded)
        {
            std::printf("loaded %llu slices from %s\n", loaded, path.c_str());
        }
        return LoadResult::Loaded;
    }

private:

    /*
    The slice a move lands in, derived arithmetically from the slice it left. The board
    scan this replaces was the hottest thing in the solver, and it was recomputing
    something already implied by the move.
    */
    static SliceKey successorKey(const SliceKey &key,
                                 unsigned int moved,
                                 unsigned int captured,
                                 unsigned int end)
    {
        SliceKey next = key;
        if (captured)
        {
            next.material &= ~materialBitOf(captured);
            if (captured == PIECE_PAWN)
            {
                next.whitePawn = static_cast<unsigned char>(NO_SQUARE);
            }
            else if (captured == (PIECE_PAWN | COLOUR_BLACK))
            {
                next.blackPawn = static_cast<unsigned char>(NO_SQUARE);
            }
        }
        if (moved == PIECE_PAWN)
        {
            next.whitePawn = static_cast<unsigned char>(end);
        }
        else if (moved == (PIECE_PAWN | COLOUR_BLACK))
        {
            next.blackPawn = static_cast<unsigned char>(end);
        }
        return next;
    }

    // Compact code for "what kind of slice-leaving move is this", used to index the cache.
    static unsigned int successorCode(const SliceKey &key,
                                      unsigned int moved,
                                      unsigned int captured,
                                      unsigned int end)
    {
        unsigned int pawnStep = 0;
        if (moved == PIECE_PAWN)
        {
            pawnStep = 1 + (end - key.whitePawn); // 2 or 3
        }
        else if (moved == (PIECE_PAWN | COLOUR_BLACK))
        {
            pawnStep = 3 + (key.blackPawn - end); // 4 or 5
        }
        return captured | (pawnStep << 4);
    }

    const SuccessorTarget &resolveSuccessor(std::array<SuccessorTarget, 128> &cache,
                                            const SliceKey &key,
                                            unsigned int moved,
                                            unsigned int captured,
                                            unsigned int end) const
    {
        SuccessorTarget &target = cache[successorCode(key, moved, captured, end)];
        if (!target.resolved)
        {
            SliceKey next = successorKey(key, moved, captured, end);
            target.mirrored = useMirror && !isCanonicalSlice(next);
            if (target.mirrored)
            {
                next = mirrorSliceKey(next);
            }
            target.slice = find(next);
            target.resolved = true;
        }
        return target;
    }

    Value successorValue(const SuccessorTarget &target, unsigned long long board, bool whiteToMove) const
    {
        if (!target.slice)
        {
            return VALUE_UNRESOLVED;
        }
        if (target.mirrored)
        {
            board = mirrorBoard(board);
            whiteToMove = !whiteToMove;
        }
        const unsigned long long index = target.slice->slice.encode(board);
        if (index == Slice::INVALID)
        {
            return VALUE_UNRESOLVED;
        }
        return target.slice->at(index, whiteToMove);
    }

    static bool leavesSlice(unsigned long long board, unsigned char move)
    {
        return isPawn(getNthNibble(board, move >> 4)) || !isEmpty(getNthNibble(board, move & 15u));
    }

    void solveSlice(const SliceKey &key, bool verbose)
    {
        SolvedSlice entry;
        if (!entry.slice.build(key))
        {
            return; // no placements satisfy this slice's invariants
        }

        unsigned long long encodeFailures = 0;
        uint64_t transient = 0;
        const auto started = std::chrono::steady_clock::now();

        std::vector<uint8_t> state;
        if (mode == Mode::Wdl)
        {
            transient = solveSliceWdl(entry, key, state, encodeFailures);
        }
        else
        {
            transient = solveSliceDtm(entry, key, state, encodeFailures);
        }

        // Accounting, reported from white's point of view so the numbers compare across
        // slices. A mirrored run only solves half the slices, so these are the counts for
        // what was actually stored, not for the whole game.
        const unsigned long long slots = 2 * entry.slice.size();
        unsigned long long illegal = 0, whiteWins = 0, blackWins = 0, draws = 0;
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            const uint8_t label = state[slot];
            if (label == STATE_ILLEGAL)
            {
                ++illegal;
                continue;
            }
            const bool whiteToMove = (slot % 2) == 0;
            if (label == STATE_WIN)
            {
                (whiteToMove ? whiteWins : blackWins) += 1;
            }
            else if (label == STATE_LOSS)
            {
                (whiteToMove ? blackWins : whiteWins) += 1;
            }
            else
            {
                ++draws;
            }
        }

        totals.slices += 1;
        totals.positions += slots;
        totals.illegal += illegal;
        totals.whiteWins += whiteWins;
        totals.blackWins += blackWins;
        totals.draws += draws;
        totals.encodeFailures += encodeFailures;
        totals.tableBytes += entry.bytes();
        totals.peakTransientBytes = std::max(totals.peakTransientBytes, transient);

        if (verbose)
        {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::printf("  %-28s %11llu positions  W %-10llu B %-10llu draw %-10llu  %6.2fs%s\n",
                        describeSlice(key).c_str(), slots, whiteWins, blackWins, draws, elapsed,
                        encodeFailures ? "  [ENCODE FAILURES]" : "");
            std::fflush(stdout);
        }

        // Written before the map takes ownership, so a slice reaches the disk the moment
        // it is finished rather than at the end of the run.
        if (checkpoint)
        {
            writeSlice(checkpoint, key, entry);
            std::fflush(checkpoint);
        }

        solved.emplace(key.packed(), std::move(entry));
    }

    /*
    Win/draw/loss only. Without distances there is nothing to order, so a plain worklist
    replaces the priority queue and a position is committed the moment it is decided.
    */
    uint64_t solveSliceWdl(SolvedSlice &entry,
                           const SliceKey &key,
                           std::vector<uint8_t> &state,
                           unsigned long long &encodeFailures)
    {
        const unsigned long long n = entry.slice.size();
        const unsigned long long slots = 2 * n;

        // The worklist stores slot numbers as 32-bit values. The largest slice in this
        // game has 645,765,120 of them, comfortably inside the range, but a wider board
        // would break that silently rather than loudly.
        if (slots > 0xFFFFFFFFull)
        {
            std::fprintf(stderr, "slice %s has %llu slots, too many for a 32-bit worklist\n",
                         describeSlice(key).c_str(), slots);
            std::abort();
        }

        state.assign(slots, STATE_UNRESOLVED);
        std::vector<uint8_t> counter(slots, 0);
        std::vector<uint32_t> worklist;

        std::array<SuccessorTarget, 128> successors{};
        unsigned char moves[MAX_MOVES];

        // Pass 1: terminal positions, plus everything already decided by a move that
        // leaves the slice into a table solved earlier.
        for (unsigned long long index = 0; index < n; ++index)
        {
            const unsigned long long board = entry.slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const unsigned long long slot = 2 * index + side;

                // A position where the side that just moved left its own king attacked
                // could never have arisen.
                if (isInCheck(board, !whiteToMove))
                {
                    state[slot] = STATE_ILLEGAL;
                    continue;
                }

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                if (count == 0)
                {
                    if (isInCheck(board, whiteToMove))
                    {
                        state[slot] = STATE_LOSS; // mated where it stands
                        worklist.push_back(static_cast<uint32_t>(slot));
                    }
                    else
                    {
                        state[slot] = STATE_DRAW; // stalemate
                    }
                    continue;
                }

                counter[slot] = static_cast<uint8_t>(count);

                for (unsigned int i = 0; i < count; ++i)
                {
                    const unsigned int start = moves[i] >> 4, end = moves[i] & 15u;
                    const unsigned int moved = getNthNibble(board, start);
                    const unsigned int captured = getNthNibble(board, end);
                    if (!isPawn(moved) && isEmpty(captured))
                    {
                        continue; // stays in the slice; the backward pass handles it
                    }

                    const SuccessorTarget &target =
                        resolveSuccessor(successors, key, moved, captured, end);
                    const Value successor =
                        successorValue(target, applyMoveToBoard(board, moves[i]), !whiteToMove);
                    if (!isPlayable(successor))
                    {
                        ++encodeFailures;
                        continue;
                    }

                    if (successor < 0)
                    {
                        state[slot] = STATE_WIN; // they are lost, so we win
                        worklist.push_back(static_cast<uint32_t>(slot));
                        break;
                    }
                    if (successor > 0)
                    {
                        --counter[slot];
                    }
                    // A drawn successor removes the chance of losing, so the counter
                    // deliberately stays put: it can now never reach zero.
                }

                if (state[slot] == STATE_UNRESOLVED && counter[slot] == 0)
                {
                    state[slot] = STATE_LOSS; // every move walked into a win for them
                    worklist.push_back(static_cast<uint32_t>(slot));
                }
            }
        }

        const uint64_t peak = state.capacity() + counter.capacity() +
                              static_cast<uint64_t>(worklist.capacity()) * sizeof(uint32_t);

        // Pass 2: backward induction inside the slice.
        while (!worklist.empty())
        {
            const uint32_t slot = worklist.back();
            worklist.pop_back();

            const bool winning = state[slot] == STATE_WIN;
            const bool whiteToMove = (slot % 2) == 0;
            const unsigned long long board = entry.slice.decode(slot / 2);

            forEachPredecessor(
                board, whiteToMove, entry.slice,
                [&](unsigned long long predecessorIndex)
                {
                    const unsigned long long predecessorSlot =
                        2 * predecessorIndex + (whiteToMove ? 1 : 0);
                    if (state[predecessorSlot] != STATE_UNRESOLVED)
                    {
                        return;
                    }
                    if (!winning)
                    {
                        // The mover here is lost, so whoever moved into it has a win.
                        state[predecessorSlot] = STATE_WIN;
                        worklist.push_back(static_cast<uint32_t>(predecessorSlot));
                    }
                    else if (counter[predecessorSlot] > 0 && --counter[predecessorSlot] == 0)
                    {
                        state[predecessorSlot] = STATE_LOSS;
                        worklist.push_back(static_cast<uint32_t>(predecessorSlot));
                    }
                },
                encodeFailures);
        }

        // Whatever survived the fixpoint unlabelled is a draw: neither side can force a
        // result, so the game goes on forever.
        entry.wdl.assign(slots);
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            switch (state[slot])
            {
            case STATE_WIN:
                entry.wdl.set(slot, PackedWdl::CODE_WIN);
                break;
            case STATE_LOSS:
                entry.wdl.set(slot, PackedWdl::CODE_LOSS);
                break;
            case STATE_ILLEGAL:
                entry.wdl.set(slot, PackedWdl::CODE_ILLEGAL);
                break;
            default:
                entry.wdl.set(slot, PackedWdl::CODE_DRAW);
                state[slot] = STATE_DRAW;
                break;
            }
        }
        return peak;
    }

    /*
    Win/draw/loss with exact distance to mate. Positions must commit in ascending
    distance for the distances to come out minimal, which is what the priority queue
    buys; the cost is two bytes per position instead of two bits.
    */
    uint64_t solveSliceDtm(SolvedSlice &entry,
                           const SliceKey &key,
                           std::vector<uint8_t> &state,
                           unsigned long long &encodeFailures)
    {
        const unsigned long long n = entry.slice.size();
        const unsigned long long slots = 2 * n;

        entry.dtm.assign(slots, VALUE_UNRESOLVED);
        std::vector<uint8_t> counter(slots, 0);
        std::vector<Value> lossValue(slots, VALUE_UNRESOLVED);

        struct Pending
        {
            int32_t distance;
            unsigned long long slot;
            Value value;
            bool operator>(const Pending &other) const { return distance > other.distance; }
        };
        std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> queue;
        const auto propose = [&](unsigned long long slot, Value value)
        { queue.push({distanceToMate(value), slot, value}); };

        std::array<SuccessorTarget, 128> successors{};
        unsigned char moves[MAX_MOVES];

        for (unsigned long long index = 0; index < n; ++index)
        {
            const unsigned long long board = entry.slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const unsigned long long slot = 2 * index + side;

                if (isInCheck(board, !whiteToMove))
                {
                    entry.dtm[slot] = VALUE_ILLEGAL;
                    continue;
                }

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                if (count == 0)
                {
                    if (isInCheck(board, whiteToMove))
                    {
                        // Queued rather than committed so predecessors still see it in
                        // distance order.
                        propose(slot, static_cast<Value>(-1));
                    }
                    else
                    {
                        entry.dtm[slot] = VALUE_DRAW;
                    }
                    continue;
                }

                counter[slot] = static_cast<uint8_t>(count);

                for (unsigned int i = 0; i < count; ++i)
                {
                    const unsigned int start = moves[i] >> 4, end = moves[i] & 15u;
                    const unsigned int moved = getNthNibble(board, start);
                    const unsigned int captured = getNthNibble(board, end);
                    if (!isPawn(moved) && isEmpty(captured))
                    {
                        continue;
                    }

                    const SuccessorTarget &target =
                        resolveSuccessor(successors, key, moved, captured, end);
                    const Value successor =
                        successorValue(target, applyMoveToBoard(board, moves[i]), !whiteToMove);
                    if (!isPlayable(successor))
                    {
                        ++encodeFailures;
                        continue;
                    }

                    const Value ours = negateValue(successor, true);
                    if (successor < 0)
                    {
                        propose(slot, ours);
                    }
                    else if (successor > 0)
                    {
                        lossValue[slot] = std::min(lossValue[slot], ours);
                        if (--counter[slot] == 0)
                        {
                            propose(slot, lossValue[slot]);
                        }
                    }
                }
            }
        }

        const uint64_t peak = entry.dtm.capacity() * sizeof(Value) + counter.capacity() +
                              lossValue.capacity() * sizeof(Value) +
                              queue.size() * sizeof(Pending);

        while (!queue.empty())
        {
            const Pending pending = queue.top();
            queue.pop();
            if (isResolved(entry.dtm[pending.slot]))
            {
                continue; // a shorter distance already claimed this position
            }
            entry.dtm[pending.slot] = pending.value;

            const bool whiteToMove = (pending.slot % 2) == 0;
            const unsigned long long board = entry.slice.decode(pending.slot / 2);

            forEachPredecessor(
                board, whiteToMove, entry.slice,
                [&](unsigned long long predecessorIndex)
                {
                    const unsigned long long predecessorSlot =
                        2 * predecessorIndex + (whiteToMove ? 1 : 0);
                    if (isResolved(entry.dtm[predecessorSlot]))
                    {
                        return;
                    }
                    if (pending.value < 0)
                    {
                        propose(predecessorSlot, negateValue(pending.value, true));
                    }
                    else
                    {
                        const Value ours = negateValue(pending.value, true);
                        lossValue[predecessorSlot] = std::min(lossValue[predecessorSlot], ours);
                        if (counter[predecessorSlot] > 0 && --counter[predecessorSlot] == 0)
                        {
                            propose(predecessorSlot, lossValue[predecessorSlot]);
                        }
                    }
                },
                encodeFailures);
        }

        state.assign(slots, STATE_UNRESOLVED);
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            if (entry.dtm[slot] == VALUE_UNRESOLVED)
            {
                entry.dtm[slot] = VALUE_DRAW;
            }
            const Value value = entry.dtm[slot];
            state[slot] = (value == VALUE_ILLEGAL) ? STATE_ILLEGAL
                          : (value > 0)            ? STATE_WIN
                          : (value < 0)            ? STATE_LOSS
                                                   : STATE_DRAW;
        }
        return peak;
    }
};

} // namespace retro
