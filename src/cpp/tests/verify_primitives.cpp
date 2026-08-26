// Exhaustive / randomized audit of the engine's bit-level primitives against naive
// reference implementations. Unlike perft and verify_attacks, this needs no
// mapping.txt — the references are written inline — and it covers inputs the old
// table never held: occupancies where the moving piece's own square is empty, and
// boards with fewer than 2 or more than 12 pieces.
//
// Run from the repository root:  ./src/cpp/compiled/verify_primitives
#include "position.h"
#include "retro/slice.h"
#include "retro/solver.h"

#include <cstdio>
#include <random>

static unsigned long long failures = 0;

#define CHECK(cond, ...)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            if (failures < 20) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

// Naive ray: walk from `square` in steps of `step`, stop at first occupied square
// (inclusive) or the edge.
static unsigned int naiveRay(unsigned int square, unsigned int occupancy, int step)
{
    unsigned int out = 0;
    for (int s = static_cast<int>(square) + step; s >= 0 && s < 16; s += step)
    {
        out |= attacks::squareBit(static_cast<unsigned int>(s));
        if (occupancy & attacks::squareBit(static_cast<unsigned int>(s)))
        {
            break;
        }
    }
    return out;
}

int main()
{
    // 1. rookAttacks / bishopAttacks / queenAttacks vs naive walk, exhaustively:
    //    16 squares x 65536 occupancies. Covers occupancies the mapping.txt oracle
    //    never did (own square empty, <2 or >12 pieces).
    for (unsigned int sq = 0; sq < 16; ++sq)
    {
        for (unsigned int occ = 0; occ < 65536; ++occ)
        {
            const unsigned int rook = naiveRay(sq, occ, 1) | naiveRay(sq, occ, -1);
            const unsigned int bishop = naiveRay(sq, occ, 2) | naiveRay(sq, occ, -2);
            CHECK(attacks::rookAttacks(sq, occ) == rook, "rook sq=%u occ=%04x", sq, occ);
            CHECK(attacks::bishopAttacks(sq, occ) == bishop, "bishop sq=%u occ=%04x", sq, occ);
            CHECK(attacks::queenAttacks(sq, occ) == (rook | bishop), "queen sq=%u occ=%04x", sq, occ);
        }
    }
    std::printf("ray attacks: 16 x 65536 x 3 exhaustive vs naive walk\n");

    // 2. Nibble helpers + occupancy/mirror on random boards built from real pieces.
    std::mt19937_64 rng(12345);
    const unsigned int pieces[] = {0, 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15};
    for (int iter = 0; iter < 200000; ++iter)
    {
        unsigned long long board = 0;
        for (unsigned int s = 0; s < 16; ++s)
        {
            board = insertNthNibble(board, pieces[rng() % 13], s);
        }

        // occupancy via naive scan
        unsigned int occ = 0, black = 0;
        for (unsigned int s = 0; s < 16; ++s)
        {
            const unsigned int nib = static_cast<unsigned int>(getNthNibble(board, s));
            if (nib) occ |= attacks::squareBit(s);
            if (nib & 8) black |= attacks::squareBit(s);
        }
        CHECK(getOccupancy(board) == occ, "occupancy");
        CHECK(getBlackOccupancy(board) == black, "black occupancy");
        CHECK(getPlayerOccupancy(board, true) == (occ & ~black), "white occupancy");

        // getPieceSquares vs scan, for every piece nibble
        for (const unsigned int p : pieces)
        {
            if (!p) continue;
            unsigned int expect = 0;
            for (unsigned int s = 0; s < 16; ++s)
                if (getNthNibble(board, s) == p) expect |= attacks::squareBit(s);
            CHECK(getPieceSquares(board, p) == expect, "pieceSquares p=%u", p);
        }

        // mirrorBoard: involution, and per-square correctness
        const unsigned long long m = mirrorBoard(board);
        CHECK(mirrorBoard(m) == board, "mirror involution");
        for (unsigned int s = 0; s < 16; ++s)
        {
            const unsigned int a = static_cast<unsigned int>(getNthNibble(board, s));
            const unsigned int b = static_cast<unsigned int>(getNthNibble(m, 15 - s));
            const unsigned int want = a ? (a ^ 8u) : 0u;
            CHECK(b == want, "mirror nibble s=%u", s);
        }

        // fence round trip
        const string fence = varsToFence(board, true, 7, 42);
        unsigned long long b2 = 0; bool act = false; unsigned int hm = 0, fm = 0;
        tie(b2, act, hm, fm) = fenceToVars(fence, b2, act, hm, fm);
        CHECK(b2 == board && act && hm == 7 && fm == 42, "fence round trip: %s", fence.c_str());
    }
    std::printf("nibble/occupancy/mirror/fence: 200k random boards\n");

    // 3. PackedWdl: random set/get pattern vs a plain array.
    {
        retro::PackedWdl packed;
        const uint64_t n = 100003;  // odd size straddling word boundaries
        packed.assign(n);
        std::vector<uint8_t> ref(n, 0);
        for (int iter = 0; iter < 2000000; ++iter)
        {
            const uint64_t i = rng() % n;
            const uint8_t code = static_cast<uint8_t>(rng() & 3);
            packed.set(i, code);
            ref[i] = code;
        }
        for (uint64_t i = 0; i < n; ++i)
        {
            CHECK(packed.get(i) == ref[i], "packedwdl at %llu", (unsigned long long)i);
        }
        std::printf("PackedWdl: 2M random writes, full readback\n");
    }

    // 4. negateValue: involution on DTM-encoded values, and outcome flip.
    for (int v = -400; v <= 400; ++v)
    {
        if (v == 0) continue;
        const retro::Value val = static_cast<retro::Value>(v);
        const retro::Value neg = retro::negateValue(val, true);
        CHECK((val > 0) == (neg < 0), "negate sign v=%d", v);
        CHECK(retro::distanceToMate(neg) == retro::distanceToMate(val) + 1,
              "negate distance v=%d", v);
    }
    CHECK(retro::negateValue(0, true) == 0, "negate draw");
    std::printf("negateValue: sign flip + distance+1 over [-400,400]\n");

    // 5. Slice key mirror: involution and canonical-pair consistency over all keys.
    {
        unsigned long long keys = 0;
        for (const retro::SliceKey &key : retro::allSliceKeys())
        {
            ++keys;
            const retro::SliceKey m = retro::mirrorSliceKey(key);
            CHECK(retro::mirrorSliceKey(m).packed() == key.packed(), "slicekey involution");
            CHECK(retro::isCanonicalSlice(key) || retro::isCanonicalSlice(m),
                  "one of each mirror pair canonical");
            CHECK(key.solveRank() == m.solveRank(), "mirror pair same solve rank");
        }
        std::printf("slice keys: involution/canonical/rank over %llu keys\n", keys);
    }

    // 6. Slice decode->encode already covered by `retro verify`; here spot-check that
    //    mirrored boards encode into the mirrored slice with the same index semantics
    //    used by the solver (value equality is checked by mirrorcheck; this checks
    //    only that encode succeeds where the solver assumes it does).
    {
        retro::Slice slice, mslice;
        unsigned long long checked = 0;
        for (const retro::SliceKey &key : retro::allSliceKeys())
        {
            if (key.pieceCount() > 4 || !slice.build(key)) continue;
            const retro::SliceKey mkey = retro::mirrorSliceKey(key);
            if (!mslice.build(mkey)) { CHECK(false, "mirror slice empty"); continue; }
            for (unsigned long long i = 0; i < slice.size(); ++i)
            {
                const unsigned long long board = slice.decode(i);
                CHECK(mslice.encode(mirrorBoard(board)) != retro::Slice::INVALID,
                      "mirror encode failed");
                ++checked;
            }
        }
        std::printf("mirror-encode: %llu placements at <=4 pieces\n", checked);
    }

    std::printf("\n%s (%llu failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
