// The cost of the operations of the Protocol family (foundation/core/Protocol.h)
// against the hand forms that they replace and against state-of-the-art
// references.
//
//   hash<Small>, hash<Wide>   the fold of the family, against foldhash-f and
//                             against the pinned rapidhash
//   encode, decode            the byte codec of a slot of a memory plan (40
//                             bytes), against a copy of its 40 bytes
//   feedback_signature,       the product folds on the hash of the family,
//   loopterm_hash             against their packed forms
//   hash of the parts         the hash of a record of sums, of a bounded list
//                             and of a heap array, each against a record of
//                             fixed size with the same bytes of image
//   decode of a heap array    the decode of the image of a heap record,
//                             against a hand decode: a bit copy of the image
//                             into a record of fixed size, and one copy of
//                             its words into a new HeapArray
//
// THE REFERENCES.  foldhash-f of Orson Peters (2024, the default hasher of
// hashbrown and of the Rust standard tables) packs the fields into a 128-bit
// sponge and runs one folded 64x64 multiply for each 16 bytes.  The bench
// holds a port of its integer path with a fixed seed, so a run is
// deterministic.  The hash of the family runs the same step over the blocks
// of the image, so on Small and on Wide it gives the bits of the port.  The
// crate is Rust, and a call from C++ cannot inline it.  rapidhash v3 of
// Nicolas De Carli (utils/pins.txt) is the hash of a short byte string of
// the state of the art in C: rapidhashNano for a key of at most 48 bytes,
// and rapidhash for a longer key.  It reads the bytes of each record, which
// are the image of the record, because Small and Wide hold no padding.
//
// The rows of one record measure one hash of a record that a volatile word
// changes.  The rows of a batch measure the hash of 64 records of memory,
// and the reference rows call a door of their own unit for each batch.

#include <cstdint>
#include <utility>

#include <crucible/MerkleDag.h>  // FeedbackEdge, LoopTermKind, NumericalRecipe, TensorSlot
#include <crucible/NumericalRecipe.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Mix.h>

#include "bench_harness.h"
#include "bench_reflect_references.h"

namespace {

struct[[= ::fixy::derive_protocol{}]] Small {
    uint32_t id;
    uint16_t kind;
    uint16_t flags;
    int64_t payload;
};

struct[[= ::fixy::derive_protocol{}]] Wide {
    uint64_t schema;
    uint64_t shape;
    uint64_t scope;
    uint64_t callsite;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint8_t op_flags;
    uint8_t pad[7];
    int64_t scalars[5];
};

// The count of records of a batch row.
constexpr std::size_t kBatch = 64;

using SmallBatch = ::fixy::FixedArray<Small, kBatch>;
using WideBatch = ::fixy::FixedArray<Wide, kBatch>;

// The rows of the sums, the bounded lists and the heap parts.  A sum and a
// list write their image on the stack first, and the fold reads that image
// as bytes.  The reference of each is a record of the same count of plain
// bytes, whose fold reads its words in place.  A heap record folds its count
// and its words one after the other, and one block of its length.  Its
// reference is a record of fixed size with the same 264 bytes of image.
enum class SumCode : std::uint8_t {
    done = 1,
    stalled = 2,
};

struct[[= ::fixy::derive_protocol{}]] SumRecord {
    ::fixy::Option<std::uint64_t> maybe{};
    ::fixy::Result<std::uint32_t, SumCode> outcome{std::uint32_t{0}};
    ::fixy::OneOf<std::uint16_t, std::uint64_t> either{std::uint16_t{0}};
};

struct[[= ::fixy::derive_protocol{}]] SumBytes {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<SumRecord>> image{};
};

struct[[= ::fixy::derive_protocol{}]] ListRecord {
    ::fixy::FixedList<std::uint32_t, 16> words{};
};

struct[[= ::fixy::derive_protocol{}]] ListBytes {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<ListRecord>> image{};
};

struct[[= ::fixy::derive_protocol{}]] HeapRecord {
    ::fixy::HeapArray<std::uint32_t> words{};
};

// The count of the words of the list row and of the heap row.
constexpr std::size_t kListWords = 8;
constexpr std::size_t kHeapWords = 64;

struct[[= ::fixy::derive_protocol{}]] HeapBytes {
    std::uint64_t count = kHeapWords;
    ::fixy::FixedArray<std::uint32_t, kHeapWords> words{};
};

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The budget of the decode row: one page, above the 256 bytes of the words.
constexpr ::fixy::Quantity<::fixy::unit::Byte, std::size_t> kDecodeBudget{4096};

// The next word of a splitmix64 stream: the fields of the records of a batch.
uint64_t next_word(uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    return ::foundation::reflect::fmix64(state);
}

SmallBatch make_small_batch(uint64_t seed) noexcept {
    SmallBatch batch{};
    for (Small& s : batch) {
        s.id = static_cast<uint32_t>(next_word(seed));
        s.kind = static_cast<uint16_t>(next_word(seed));
        s.flags = static_cast<uint16_t>(next_word(seed));
        s.payload = static_cast<int64_t>(next_word(seed));
    }
    return batch;
}

WideBatch make_wide_batch(uint64_t seed) noexcept {
    WideBatch batch{};
    for (Wide& w : batch) {
        w.schema = next_word(seed);
        w.shape = next_word(seed);
        w.scope = next_word(seed);
        w.callsite = next_word(seed);
        w.num_inputs = static_cast<uint32_t>(next_word(seed) & 0xF);
        w.num_outputs = static_cast<uint32_t>(next_word(seed) & 0xF);
        w.op_flags = static_cast<uint8_t>(next_word(seed));
        for (int64_t& scalar : w.scalars) {
            scalar = static_cast<int64_t>(next_word(seed));
        }
    }
    return batch;
}

// The xor of the hash of the family of each record of a batch.
template <class Batch>
uint64_t family_hash_batch(Batch const& batch) noexcept {
    uint64_t folded = 0;
    for (auto const& record : batch) {
        folded ^= ::fixy::hash(record).raw();
    }
    return folded;
}

// ── The reference: the integer path of foldhash-f ────────────────────
//
// folded_multiply is the low and the high word of the full product, xored.
// write packs each integer into the sponge, and a sponge that would pass 128
// bits folds into the accumulator first.  finish folds a sponge that holds
// bits.  The seeds are fixed constants of the reference.
// The sponge of 128 bits is two words, the low word first.  A value of bits
// bits goes in at the count of bits that the sponge holds.
class FoldhashReference {
public:
    void write(uint64_t value, unsigned bits) noexcept {
        if (sponge_bits_ + bits > 128) {
            accumulator_ = folded_multiply_(sponge_low_ ^ accumulator_, sponge_high_ ^ kSeed0);
            sponge_low_ = value;
            sponge_high_ = 0;
            sponge_bits_ = bits;
            return;
        }
        if (sponge_bits_ < 64) {
            sponge_low_ |= value << sponge_bits_;
            if (sponge_bits_ != 0 && sponge_bits_ + bits > 64) sponge_high_ |= value >> (64 - sponge_bits_);
        } else {
            sponge_high_ |= value << (sponge_bits_ - 64);
        }
        sponge_bits_ += bits;
    }

    [[nodiscard]] uint64_t finish() const noexcept {
        if (sponge_bits_ == 0) return accumulator_;
        return folded_multiply_(sponge_low_ ^ accumulator_, sponge_high_ ^ kSeed0);
    }

private:
    static constexpr uint64_t kSeed0 = 0x243f6a8885a308d3ULL;
    static constexpr uint64_t kPerHasherSeed = 0x13198a2e03707344ULL;

    [[nodiscard]] static uint64_t folded_multiply_(uint64_t left, uint64_t right) noexcept {
        const auto product = ::fixy::widening_mul(left, right);
        return product.low ^ product.high;
    }

    uint64_t accumulator_ = kPerHasherSeed;
    uint64_t sponge_low_ = 0;
    uint64_t sponge_high_ = 0;
    unsigned sponge_bits_ = 0;
};

uint64_t reference_hash_small(const Small& s) noexcept {
    FoldhashReference hasher{};
    hasher.write(s.id, 32);
    hasher.write(s.kind, 16);
    hasher.write(s.flags, 16);
    hasher.write(static_cast<uint64_t>(s.payload), 64);
    return hasher.finish();
}

uint64_t reference_hash_wide(const Wide& w) noexcept {
    FoldhashReference hasher{};
    hasher.write(w.schema, 64);
    hasher.write(w.shape, 64);
    hasher.write(w.scope, 64);
    hasher.write(w.callsite, 64);
    hasher.write(w.num_inputs, 32);
    hasher.write(w.num_outputs, 32);
    hasher.write(w.op_flags, 8);
    for (const uint8_t byte : w.pad) {
        hasher.write(byte, 8);
    }
    for (const int64_t scalar : w.scalars) {
        hasher.write(static_cast<uint64_t>(scalar), 64);
    }
    return hasher.finish();
}

uint64_t reference_hash_small_batch(SmallBatch const& batch) noexcept {
    uint64_t folded = 0;
    for (Small const& s : batch) {
        folded ^= reference_hash_small(s);
    }
    return folded;
}

uint64_t reference_hash_wide_batch(WideBatch const& batch) noexcept {
    uint64_t folded = 0;
    for (Wide const& w : batch) {
        folded ^= reference_hash_wide(w);
    }
    return folded;
}

// ── The reference: the pinned rapidhash ───────────────────────────────

#if defined(CRUCIBLE_BENCH_RAPIDHASH)
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the bytes of a batch go to the door of the pinned rapidhash")

static_assert(sizeof(Small) == 16 && sizeof(Wide) == 88, "the bytes of a record are its image, with no padding byte");

uint64_t rapidhash_small_batch(SmallBatch const& batch) noexcept {
    Small const& first = batch.at(::fixy::index<0>());
    return bench_reflect_references::rapidhash_nano_16(static_cast<std::byte const*>(static_cast<void const*>(&first)),
                                                       kBatch);
}

uint64_t rapidhash_wide_batch(WideBatch const& batch) noexcept {
    Wide const& first = batch.at(::fixy::index<0>());
    return bench_reflect_references::rapidhash_88(static_cast<std::byte const*>(static_cast<void const*>(&first)),
                                                  kBatch);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#endif

// ── The packed form of feedback_signature ──────────────────────────
//
// One shift and one or for each edge, and one fmix64 for each edge.  The
// product fold takes the hash of the family of each edge instead.
uint64_t feedback_signature_packed(::fixy::View<const crucible::FeedbackEdge> edges) noexcept {
    if (edges.size() == 0) return 0;
    constexpr uint64_t kSeed = 0x6665656462616B73ULL;
    uint64_t h = kSeed;
    for (const auto& e : edges) {
        uint64_t packed = (static_cast<uint64_t>(e.output_idx) << 16) | e.input_idx;
        h = ::foundation::reflect::fmix64(h ^ packed);
    }
    h = ::foundation::reflect::fmix64(h ^ edges.size());
    return h;
}

// ── The packed form of loopterm_hash ───────────────────────────────
//
// term_kind (1 byte) and repeat_count (4 bytes) in one word, and epsilon by
// its bits with a separate salt: two fmix64 calls in total.
uint64_t loopterm_hash_packed(crucible::LoopTermKind term_kind, uint32_t repeat_count, float epsilon) noexcept {
    constexpr uint64_t kTermSalt = 0x7465726D696E6174ULL;
    constexpr uint64_t kEpsSalt = 0x65707369006C6F6EULL;
    uint64_t packed = static_cast<uint64_t>(std::to_underlying(term_kind)) | (static_cast<uint64_t>(repeat_count) << 8);
    uint64_t h = ::foundation::reflect::fmix64(packed ^ kTermSalt);
    h ^= ::foundation::reflect::fmix64(static_cast<uint64_t>(::fixy::bit_cast<uint32_t>(epsilon)) ^ kEpsSalt);
    return h;
}

// ── The family hash of the fields of a recipe ──────────────────────
//
// compute_recipe_hash packs the eight one-byte fields into one word and
// mixes it one time.  The hash of the family packs the eight bytes of the
// image into one block and folds it one time.
struct[[= ::fixy::derive_protocol{}]] RecipeSpec {
    crucible::ScalarType accum_dtype;
    crucible::ScalarType out_dtype;
    crucible::ReductionAlgo reduction_algo;
    ::foundation::core::RoundingMode rounding;
    crucible::ScalePolicy scale_policy;
    crucible::SoftmaxRecurrence softmax;
    crucible::ReductionDeterminism determinism;
    uint8_t flags;
};

uint64_t compute_recipe_hash_reflected(const crucible::NumericalRecipe& r) noexcept {
    return ::fixy::hash(RecipeSpec{r.accum_dtype, r.out_dtype, r.reduction_algo, r.rounding, r.scale_policy, r.softmax,
                                   r.determinism, r.flags.raw()})
        .raw();
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== protocol hash ===\n\n");

    // Each Run uses volatile-seeded inputs so the compiler can't hoist the
    // hash computation out of the body. The seeds live in the outer scope so
    // the IIFE-lambdas can capture by reference.
    volatile uint32_t vid = 42;
    volatile uint16_t vkind = 7;
    volatile uint16_t vflags = 0x3F;
    volatile int64_t vpayload = 0x123456789ABCDEF0LL;
    volatile uint64_t vschema = 0xAABBCCDD00000000ULL;

    auto make_small = [&]() noexcept {
        Small s{};
        s.id = vid;
        s.kind = vkind;
        s.flags = vflags;
        s.payload = vpayload;
        return s;
    };

    auto make_wide = [&]() noexcept {
        Wide w{};
        w.schema = vschema;
        w.shape = vschema ^ 1;
        w.scope = vschema ^ 2;
        w.callsite = vschema ^ 3;
        w.num_inputs = 2;
        w.num_outputs = 1;
        w.op_flags = 0x1;
        w.scalars[0] = 1;
        w.scalars[1] = 2;
        return w;
    };

    volatile uint64_t vbatch_seed = 0x50524F544F434F4CULL;
    SmallBatch small_batch = make_small_batch(vbatch_seed);
    WideBatch wide_batch = make_wide_batch(vbatch_seed);

    ::fixy::FixedArray<bench::Report, 8> reports{
        bench::run("hash<Small>          (4 fields)",
                   [&] {
                       Small s = make_small();
                       auto h = ::fixy::hash(s).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("foldhash-f port      (same 4)",
                   [&] {
                       Small s = make_small();
                       auto h = reference_hash_small(s);
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<Wide>           (19 fields)",
                   [&] {
                       Wide w = make_wide();
                       auto h = ::fixy::hash(w).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("foldhash-f port      (same 19)",
                   [&] {
                       Wide w = make_wide();
                       auto h = reference_hash_wide(w);
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<Small> x64      (16 bytes)",
                   [&] {
                       bench::do_not_optimize(small_batch);
                       auto h = family_hash_batch(small_batch);
                       bench::do_not_optimize(h);
                   }),
        bench::run("foldhash-f port x64  (16 bytes)",
                   [&] {
                       bench::do_not_optimize(small_batch);
                       auto h = reference_hash_small_batch(small_batch);
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<Wide> x64       (88 bytes)",
                   [&] {
                       bench::do_not_optimize(wide_batch);
                       auto h = family_hash_batch(wide_batch);
                       bench::do_not_optimize(h);
                   }),
        bench::run("foldhash-f port x64  (88 bytes)",
                   [&] {
                       bench::do_not_optimize(wide_batch);
                       auto h = reference_hash_wide_batch(wide_batch);
                       bench::do_not_optimize(h);
                   }),
    };

    bench::emit_reports_text(reports.view());

    // The hash of the family and the port of foldhash-f run one step over
    // the same blocks, so the pairs of one record and of a batch must be
    // indistinguishable on a clean machine.  Each line of a compare names its
    // two rows.
    bench::print("\n=== compare ===\n");
    bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())).print_text();
    bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>())).print_text();
    bench::compare(reports.at(::fixy::index<4>()), reports.at(::fixy::index<5>())).print_text();
    bench::compare(reports.at(::fixy::index<6>()), reports.at(::fixy::index<7>())).print_text();

    // The batch rows of the pinned rapidhash give the ratio to the state of
    // the art.  A tools directory with no rapidhash at its pin gives no row.
#if defined(CRUCIBLE_BENCH_RAPIDHASH)
    ::fixy::FixedArray<bench::Report, 2> rapidhash_reports{
        bench::run("rapidhashNano x64    (16 bytes)",
                   [&] {
                       bench::do_not_optimize(small_batch);
                       auto h = rapidhash_small_batch(small_batch);
                       bench::do_not_optimize(h);
                   }),
        bench::run("rapidhash x64        (88 bytes)",
                   [&] {
                       bench::do_not_optimize(wide_batch);
                       auto h = rapidhash_wide_batch(wide_batch);
                       bench::do_not_optimize(h);
                   }),
    };
    bench::emit_reports_text(rapidhash_reports.view());
    bench::print("\n=== compare with rapidhash ===\n");
    bench::compare(reports.at(::fixy::index<4>()), rapidhash_reports.at(::fixy::index<0>())).print_text();
    bench::compare(reports.at(::fixy::index<6>()), rapidhash_reports.at(::fixy::index<1>())).print_text();
#endif

    // ═══════════════════════════════════════════════════════════════
    // The product folds on the hash of the family, against their packed
    // forms, and the byte codec of a slot of a memory plan.  The reference
    // of the codec is a copy of the 40 bytes of the slot, which is the image
    // when no part needs a check: the slot has no padding byte that the
    // codec skips.
    // ═══════════════════════════════════════════════════════════════
    bench::print("\n=== product folds and codec ===\n\n");

    volatile uint64_t vbytes = 4096;
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::TensorSlot>> image{};
    crucible::TensorSlot slot{};
    slot.nbytes = 64;
    slot.dtype = crucible::ScalarType::Float;
    slot.slot_id = crucible::SlotId{3};
    ::fixy::encode(slot, image.view());

    // Tiny FeedbackEdge (2 × u16 = 4 B); span of 8 edges per call.
    ::foundation::core::FixedArray<crucible::FeedbackEdge, 8> edges_buf{{0, 1}, {1, 2}, {2, 3}, {3, 4},
                                                                        {4, 5}, {5, 6}, {6, 7}, {7, 0}};
    volatile uint16_t bump = 0;  // prevents constant-fold

    ::fixy::FixedArray<bench::Report, 9> refactor_reports{
        bench::run("feedback_signature  (family, 8 edges)",
                   [&] {
                       edges_buf.at(::fixy::index<0>()).output_idx = bump;  // perturb each iter
                       auto h = crucible::feedback_signature(edges_buf.view());
                       bench::do_not_optimize(h);
                   }),
        bench::run("feedback_signature  (packed, 8 edges)",
                   [&] {
                       edges_buf.at(::fixy::index<0>()).output_idx = bump;
                       auto h = feedback_signature_packed(edges_buf.view());
                       bench::do_not_optimize(h);
                   }),
        bench::run("loopterm_hash       (family)",
                   [&] {
                       crucible::LoopNode ln{};
                       ln.term_kind = crucible::LoopTermKind::REPEAT;
                       ln.repeat_count = 100u + bump;
                       ln.epsilon = 1.0e-6f;
                       auto h = crucible::loopterm_hash(ln);
                       bench::do_not_optimize(h);
                   }),
        bench::run("loopterm_hash       (packed)",
                   [&] {
                       auto h = loopterm_hash_packed(crucible::LoopTermKind::REPEAT, 100u + bump, 1.0e-6f);
                       bench::do_not_optimize(h);
                   }),
        bench::run("compute_recipe_hash (packed actual)",
                   [&] {
                       crucible::NumericalRecipe r{};
                       r.accum_dtype = crucible::ScalarType::Float;
                       r.out_dtype = crucible::ScalarType::Half;
                       r.flags = ::fixy::Bits<crucible::RecipeFlags>::from_raw(static_cast<uint8_t>(bump));
                       auto h = crucible::compute_recipe_hash(r);
                       bench::do_not_optimize(h);
                   }),
        bench::run("compute_recipe_hash (family hypothetical)",
                   [&] {
                       crucible::NumericalRecipe r{};
                       r.accum_dtype = crucible::ScalarType::Float;
                       r.out_dtype = crucible::ScalarType::Half;
                       r.flags = ::fixy::Bits<crucible::RecipeFlags>::from_raw(static_cast<uint8_t>(bump));
                       auto h = compute_recipe_hash_reflected(r);
                       bench::do_not_optimize(h);
                   }),
        bench::run("encode<TensorSlot>   (40 bytes)",
                   [&] {
                       slot.offset_bytes = vbytes;
                       ::fixy::encode(slot, image.view());
                       bench::do_not_optimize(image);
                   }),
        bench::run("copy of 40 bytes     (reference)",
                   [&] {
                       slot.offset_bytes = vbytes;
                       image = ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(crucible::TensorSlot)>>(slot);
                       bench::do_not_optimize(image);
                   }),
        bench::run("decode<TensorSlot>   (40 bytes)",
                   [&] {
                       const auto& read = image;
                       auto back = ::fixy::decode<crucible::TensorSlot>(read.view());
                       bench::do_not_optimize(back);
                   }),
    };

    bench::emit_reports_text(refactor_reports.view());

    bench::print("\n=== product fold and codec compares ===\n");
    bench::compare(refactor_reports.at(::fixy::index<0>()), refactor_reports.at(::fixy::index<1>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<2>()), refactor_reports.at(::fixy::index<3>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<4>()), refactor_reports.at(::fixy::index<5>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<6>()), refactor_reports.at(::fixy::index<7>())).print_text();

    // The hash of the sums, the bounded lists and the heap parts, and the
    // decode of the image of the heap record, each against its reference.  A
    // volatile word changes one part of each hashed record in each iteration.
    // Each decode allocates the words and gives them back.
    bench::print("\n=== hash of the sums, the lists and the heap parts, decode of a heap record ===\n\n");
    volatile std::uint64_t vword = 0x0123456789ABCDEFULL;
    SumRecord sum_record{};
    sum_record.outcome = ::fixy::Result<std::uint32_t, SumCode>{::fixy::Err<SumCode>{SumCode::stalled}};
    sum_record.either = ::fixy::OneOf<std::uint16_t, std::uint64_t>{std::uint64_t{7}};
    SumBytes sum_bytes{};
    ListRecord list_record{};
    for (std::size_t index = 0; index < kListWords; ++index) {
        bool const is_put = list_record.words.append(static_cast<std::uint32_t>(index * 3)).is_ok();
        if (!is_put) ::fixy::fatal("the list row holds eight words");
    }
    ListBytes list_bytes{};
    HeapRecord heap_record{};
    heap_record.words = ::fixy::mint_heap_array_with<std::uint32_t>(
        kAlloc, kHeapWords, [](std::size_t position) noexcept { return static_cast<std::uint32_t>(position * 5); });
    HeapBytes heap_bytes{};
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<HeapBytes>> heap_image{};
    ::fixy::encode(heap_record, ::fixy::View<std::byte>{heap_image.view()});

    ::fixy::FixedArray<bench::Report, 8> part_reports{
        bench::run("hash<SumRecord>      (3 sums, 23 bytes)",
                   [&] {
                       sum_record.maybe = ::fixy::Option<std::uint64_t>::some(std::uint64_t{vword});
                       auto h = ::fixy::hash(sum_record).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<SumBytes>       (23 plain bytes)",
                   [&] {
                       sum_bytes.image.at(::fixy::index<1>()) = static_cast<std::byte>(vword);
                       auto h = ::fixy::hash(sum_bytes).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<ListRecord>     (8 of 16 words, 65 bytes)",
                   [&] {
                       *list_record.words.view().get(0).expect("the list holds a word") =
                           static_cast<std::uint32_t>(vword);
                       auto h = ::fixy::hash(list_record).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<ListBytes>      (65 plain bytes)",
                   [&] {
                       list_bytes.image.at(::fixy::index<1>()) = static_cast<std::byte>(vword);
                       auto h = ::fixy::hash(list_bytes).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<HeapRecord>     (64 words, 264 bytes)",
                   [&] {
                       *heap_record.words.view().get(0).expect("the array holds a word") =
                           static_cast<std::uint32_t>(vword);
                       auto h = ::fixy::hash(heap_record).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("hash<HeapBytes>      (the same 264 bytes)",
                   [&] {
                       heap_bytes.words.at(::fixy::index<0>()) = static_cast<std::uint32_t>(vword);
                       auto h = ::fixy::hash(heap_bytes).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("decode<HeapRecord>   (64 words, 264 bytes)",
                   [&] {
                       const auto& read = heap_image;
                       auto back = ::fixy::decode<HeapRecord>(kAlloc, kDecodeBudget,
                                                              ::fixy::View<std::byte const>{read.view()});
                       bench::do_not_optimize(back);
                   }),
        bench::run("hand decode          (the same 264 bytes)",
                   [&] {
                       const auto& read = heap_image;
                       HeapBytes const plain = ::fixy::bit_cast<HeapBytes>(read);
                       auto words = ::fixy::mint_heap_array_copy<std::uint32_t>(
                           kAlloc, ::fixy::View<std::uint32_t const>{plain.words.view()}
                                       .window(0, plain.count)
                                       .expect("the image holds its count of words"));
                       bench::do_not_optimize(words);
                   }),
    };
    bench::emit_reports_text(part_reports.view());
    bench::print("\n=== compare of the parts with their references ===\n");
    bench::compare(part_reports.at(::fixy::index<0>()), part_reports.at(::fixy::index<1>())).print_text();
    bench::compare(part_reports.at(::fixy::index<2>()), part_reports.at(::fixy::index<3>())).print_text();
    bench::compare(part_reports.at(::fixy::index<4>()), part_reports.at(::fixy::index<5>())).print_text();
    bench::compare(part_reports.at(::fixy::index<6>()), part_reports.at(::fixy::index<7>())).print_text();

    // One JSON tail holds the rows of the three groups, because the reader
    // reads only the last tail of the output.
    ::fixy::FixedList<bench::Report, 27> all_reports{};
    for (bench::Report& report : reports)
        bench::add_report(all_reports, std::move(report));
#if defined(CRUCIBLE_BENCH_RAPIDHASH)
    for (bench::Report& report : rapidhash_reports)
        bench::add_report(all_reports, std::move(report));
#endif
    for (bench::Report& report : refactor_reports)
        bench::add_report(all_reports, std::move(report));
    for (bench::Report& report : part_reports) {
        bench::add_report(all_reports, static_cast<bench::Report&&>(report));
    }
    bench::emit_reports_json(all_reports.view(), json);
    return 0;
}
