// ═══════════════════════════════════════════════════════════════════
// prop_header_roundtrip — Serialize.h header write/read round-trip.
//
// Property: for every random (kind, merkle_hash, content_hash) triple:
//   write_header(...) → bytes
//   read_header(bytes) → Header
//   header.{magic, version, kind, merkle_hash, content_hash} all equal
//   the inputs (modulo magic + version which are constants)
//
// Catches:
//   - Wire-format byte ordering bugs (write/read disagreement on
//     little-endian vs big-endian — should be invisible on x86 but
//     a future big-endian port would surface here)
//   - Header struct layout drift (a future field reorder where the
//     writer and reader disagree on offset of, say, kind)
//   - Magic / version field corruption
//
// This is a unit-style round-trip test scaled up to 100K iterations
// with random (kind × hashes) inputs.  Catches the NARROW class of
// bugs at the lowest serialization layer; full RegionNode round-trip
// would need an arena-backed generator (future work).
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

#include <array>
#include <cstdint>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::detail_ser;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("Serialize header write/read round-trip", cfg,
        [](Rng& rng) {
            struct Triple {
                TraceNodeKind kind;
                MerkleHash    mh;
                ContentHash   ch;
            };
            Triple t{};
            // Pick from real TraceNodeKind values (REGION, BRANCH,
            // LOOP, TERMINAL).  Skip arbitrary u8 values to avoid
            // hitting the invalid-enum path the reader rejects.
            static constexpr TraceNodeKind kKinds[] = {
                TraceNodeKind::REGION,
                TraceNodeKind::BRANCH,
                TraceNodeKind::LOOP,
                TraceNodeKind::TERMINAL,
            };
            t.kind = kKinds[rng.next_below(4)];
            t.mh = MerkleHash{rng.next64()};
            t.ch = ContentHash{rng.next64()};
            return t;
        },
        [](const auto& t) {
            // 32-byte buffer is the documented header size
            // (magic 4 + version 4 + kind 1 + pad 7 + merkle 8 + content 8).
            std::array<uint8_t, 32> buf{};
            Writer w{.buf = buf.data(), .pos = 0, .max = buf.size()};
            write_header(w, t.kind, t.mh, t.ch);
            if (w.pos != 32) return false;

            Reader r{.buf = buf.data(), .pos = 0, .len = buf.size()};
            const Header h = read_header(r);

            // Round-trip equality on each field.
            if (h.magic        != CDAG_MAGIC)   return false;
            if (h.version      != CDAG_VERSION) return false;
            if (h.kind         != t.kind)       return false;
            if (h.merkle_hash  != t.mh)         return false;
            if (h.content_hash != t.ch)         return false;

            // Reader cursor must be exactly at the header's end
            // (no underconsumed or overconsumed bytes).
            if (r.pos != 32) return false;

            return true;
        });
}
