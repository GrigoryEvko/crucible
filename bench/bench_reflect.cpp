// The cost of the operations of the Protocol family (foundation/core/Protocol.h)
// against the hand forms that they replace and against a state-of-the-art
// reference.
//
//   hash<Small>, hash<Wide>   the fold of the family, against the hand fold of
//                             the same fields and against foldhash-f
//   encode, decode            the byte codec of a slot of a memory plan (40
//                             bytes), against a copy of its 40 bytes
//   feedback_signature,       the product folds on the hash of the family,
//   loopterm_hash             against their packed forms
//
// THE REFERENCE.  foldhash-f of Orson Peters (2024, the default hasher of
// hashbrown and of the Rust standard tables) packs the fields into a 128-bit
// sponge and runs one folded 64x64 multiply for each 16 bytes.  The bench
// holds a port of its integer path with a fixed seed, so a run is
// deterministic.  The fold of the family keeps the scheme that the store
// pins (the hash of each guard is in the merkle hash of each branch), so a
// ratio above 1 against the reference is the cost of that pin.

#include <cstdint>
#include <cstdio>
#include <utility>

#include <crucible/MerkleDag.h>  // FeedbackEdge, LoopTermKind, NumericalRecipe, TensorSlot
#include <crucible/NumericalRecipe.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/reflect/Mix.h>

#include "bench_harness.h"

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

uint64_t manual_hash_small(const Small& s) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h = h * 0x9E3779B97F4A7C15ULL ^ ::foundation::reflect::fmix64(s.id);
    h = h * 0x9E3779B97F4A7C15ULL ^ ::foundation::reflect::fmix64(s.kind);
    h = h * 0x9E3779B97F4A7C15ULL ^ ::foundation::reflect::fmix64(s.flags);
    h = h * 0x9E3779B97F4A7C15ULL ^ ::foundation::reflect::fmix64(static_cast<uint64_t>(s.payload));
    return ::foundation::reflect::fmix64(h);
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
    h ^= ::foundation::reflect::fmix64(static_cast<uint64_t>(std::bit_cast<uint32_t>(epsilon)) ^ kEpsSalt);
    return h;
}

// ── The family hash of the fields of a recipe ──────────────────────
//
// compute_recipe_hash packs the eight one-byte fields into one word and
// mixes it one time.  The hash of the family takes one fmix64 for each
// field, so the row measures the cost that the packed form keeps away.
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

    std::printf("=== protocol hash ===\n\n");

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

    ::fixy::FixedArray<bench::Report, 5> reports{
        bench::run("hash<Small>          (4 fields)",
                   [&] {
                       Small s = make_small();
                       auto h = ::fixy::hash(s).raw();
                       bench::do_not_optimize(h);
                   }),
        bench::run("manual fmix64 chain  (same 4)",
                   [&] {
                       Small s = make_small();
                       auto h = manual_hash_small(s);
                       bench::do_not_optimize(h);
                   }),
        bench::run("foldhash-f reference (same 4)",
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
        bench::run("foldhash-f reference (same 19)",
                   [&] {
                       Wide w = make_wide();
                       auto h = reference_hash_wide(w);
                       bench::do_not_optimize(h);
                   }),
    };

    bench::emit_reports_text(reports.view());

    // The hash of the family and the hand fold of the same fields are one
    // scheme, so they must be indistinguishable on a clean machine.  The
    // reference rows give the ratio to the state of the art.  Each line of a
    // compare names its two rows.
    std::printf("\n=== compare ===\n");
    bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())).print_text();
    bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<2>())).print_text();
    bench::compare(reports.at(::fixy::index<3>()), reports.at(::fixy::index<4>())).print_text();

    // ═══════════════════════════════════════════════════════════════
    // The product folds on the hash of the family, against their packed
    // forms, and the byte codec of a slot of a memory plan.  The reference
    // of the codec is a copy of the 40 bytes of the slot, which is the image
    // when no part needs a check: the slot has no padding byte that the
    // codec skips.
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n=== product folds and codec ===\n\n");

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

    std::printf("\n=== product fold and codec compares ===\n");
    bench::compare(refactor_reports.at(::fixy::index<0>()), refactor_reports.at(::fixy::index<1>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<2>()), refactor_reports.at(::fixy::index<3>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<4>()), refactor_reports.at(::fixy::index<5>())).print_text();
    bench::compare(refactor_reports.at(::fixy::index<6>()), refactor_reports.at(::fixy::index<7>())).print_text();

    // One JSON tail holds the rows of the two groups, because the reader
    // reads only the last tail of the output.
    ::fixy::FixedList<bench::Report, 14> all_reports{};
    for (bench::Report& report : reports)
        bench::add_report(all_reports, std::move(report));
    for (bench::Report& report : refactor_reports)
        bench::add_report(all_reports, std::move(report));
    bench::emit_reports_json(all_reports.view(), json);
    return 0;
}
