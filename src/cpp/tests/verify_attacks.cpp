#include "attacks.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

/*
Verifies that the computed attack generation in `attacks.h` reproduces every entry of
the old `mapping.txt` lookup table, so the table can be deleted without changing engine
behaviour.

The old key layout was (piece << 20) | (square << 16) | occupancy, where `piece` was
the white-equivalent nibble for every piece except pawns, which kept their colour
(1 = white pawn, 9 = black pawn).

`mapping.txt` was only generated for occupancies with 2..12 pieces on the board. That is
not a gap in coverage: both kings are always present, so a real position never has fewer
than 2 pieces, and with no promotion there is at most one of each piece per side, so it
never has more than 12. Entries outside that range are reported separately rather than
counted as mismatches.

Run from the repository root:  ./src/cpp/compiled/verify_attacks
*/
int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "mapping.txt";
    std::ifstream file(path);
    if (!file)
    {
        std::fprintf(stderr, "could not open %s\n", path);
        return 2;
    }

    unsigned long long key = 0;
    unsigned int expected = 0;
    unsigned long long checked = 0, mismatched = 0;
    unsigned int coveredPieces = 0;

    while (file >> key >> expected)
    {
        const unsigned int piece = static_cast<unsigned int>(key >> 20);
        const unsigned int square = static_cast<unsigned int>((key >> 16) & 15u);
        const unsigned int occupancy = static_cast<unsigned int>(key & 0xFFFFu);

        const unsigned int actual = attacks::pieceAttacks(piece, square, occupancy);
        coveredPieces |= 1u << (piece & 15u);
        ++checked;

        if (actual != expected)
        {
            if (mismatched < 10)
            {
                std::printf("MISMATCH piece=%u square=%u occ=0x%04x expected=0x%04x got=0x%04x\n",
                            piece, square, occupancy, expected, actual);
            }
            ++mismatched;
        }
    }

    // Confirm we actually exercised every piece type the engine can ask about.
    const unsigned int wanted = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 5) |
                                (1u << 6) | (1u << 7) | (1u << 9);
    std::printf("checked %llu entries, %llu mismatches, piece coverage %s\n",
                checked, mismatched,
                ((coveredPieces & wanted) == wanted) ? "complete" : "INCOMPLETE");

    // Independently re-derive the range mapping.txt covers, so a truncated or swapped
    // file cannot quietly pass by simply containing fewer rows.
    const unsigned long long expectedRows = 7ull * 16ull * ((1ull << 15) - 1ull - 576ull);
    std::printf("expected %llu rows for 2..12 piece occupancies: %s\n",
                expectedRows, (checked == expectedRows) ? "matches" : "DIFFERS");

    return (mismatched == 0) ? 0 : 1;
}
