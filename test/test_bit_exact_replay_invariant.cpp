// The DetSafe axiom, as a gate: same(inputs) => same(outputs).
//
// CLAUDE.md §XIII names this test first among the load-bearing suite and
// says that if it reddens, the work stops, because a red line here means
// hidden state entered the pipeline.  It did not exist.  Every other
// determinism check in the tree is a negative-compile fixture about a
// recipe tier, which is a different claim: those say a type cannot ask
// for a relaxed reduction, not that the pipeline produces the same bytes
// twice.
//
// Three legs, each a property rather than a restatement of the last run.
//
//   1  ADDRESS INDEPENDENCE.  The same computation is described twice with
//      different data_ptr values, out of two separate arenas, in an order
//      that puts the second build at a different address.  Each region's
//      content hash, and the merkle hash over them, must come out equal
//      between the two builds.  This is the property that
//      makes content addressing work at all (§L7): a region is the same
//      region when the computation is the same, wherever its tensors
//      happen to live.  A data_ptr absorbed into the fold would break it,
//      and the fold is where a well-meant edit would put one.
//
//   2  THE REFERENCE PIN.  Both builds must equal a committed hash.  Leg 1
//      alone cannot see a change to the fold's seed, to the order of its
//      operands, to the SIMD dimension hash, or to the traversal that
//      feeds it: those move both sides together.  A count cannot pin this
//      and neither can a self-comparison.  The hash IS the content, so the
//      pin is the hash.
//
//      A legitimate change to the format reddens this test.  That is the
//      point.  The constant is updated in the same commit that changes the
//      format, by someone who has said why, and every federation cache
//      keyed on the old value is then known to be stale rather than
//      silently wrong.
//
//   3  PLAN DETERMINISM.  The same slot set yields the same pool size and
//      the same offset for every slot, twice, and equals a committed
//      reference.  §L3 rests on this: same DFG, same plan, same addresses,
//      so the same kernel sees the same alignment and the same aliasing on
//      every run and on every machine.
//
// WHY THE VERDICT IS AN EXIT CODE.  This file uses no assertion macro.
// CRUCIBLE_ASSERT follows the build's contract semantic and
// CRUCIBLE_DEBUG_ASSERT disappears under NDEBUG, so a test written with
// them proves progressively less as the build gets closer to what ships.
// A determinism gate has to mean the same thing in the debug build, in a
// contracts-stripped release build, and in a profile-guided build, where
// the optimizer has reordered blocks and re-laid every branch from
// measured counts.  Each check here is an explicit branch with its own
// exit code, the way test_serialize_release_gate.cpp does it and for the
// same reason.
//
// WHAT THIS TEST DOES NOT COVER, so that nobody reads more into a green
// line than it earns.  It is one process on one host.  It does not compare
// across machines, across vendors, or across a reshard — those are
// cross_vendor_step_invariant and fleet_reshard_replay, and both need
// hardware this tree cannot reach yet.  It does not cover the recording
// path's own inputs: it starts from a TraceEntry array it builds itself,
// so a recorder that captured the wrong metadata would still pass.

#include <crucible/Arena.h>
#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/effects/_Capabilities.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

using crucible::Arena;
using crucible::CallsiteHash;
using crucible::DeviceType;
using crucible::Layout;
using crucible::MemoryPlan;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::SchemaHash;
using crucible::ScopeHash;
using crucible::ShapeHash;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::TensorSlot;
using crucible::TraceEntry;

constexpr uint32_t kNumOps = 64;
constexpr uint32_t kNumSlots = 12;

// A counter-based generator, so the trace is a pure function of its index
// and of nothing else.  A seeded stateful RNG would also be reproducible,
// but this cannot drift when the loop order changes, which is exactly the
// kind of change the test is here to survive.
[[nodiscard]] constexpr uint64_t mix(uint64_t value) noexcept {
    value ^= value >> 33;
    value *= 0xFF51AFD7ED558CCDULL;
    value ^= value >> 33;
    value *= 0xC4CEB9FE1A85EC53ULL;
    value ^= value >> 33;
    return value;
}

// `data_ptr_base` is the only thing that differs between the two builds.
// It stands for the run-to-run variation the allocator produces, and the
// hashes must not see it.
void build_ops(crucible::effects::Test ctx, Arena& arena, TraceEntry* ops, uint64_t data_ptr_base) noexcept {
    for (uint32_t op_index = 0; op_index < kNumOps; op_index++) {
        TraceEntry& entry = ops[op_index];
        entry = TraceEntry{};
        const uint64_t seed = mix(op_index);

        entry.schema_hash = SchemaHash{seed | 1u};
        entry.shape_hash = ShapeHash{mix(seed)};
        entry.scope_hash = ScopeHash{mix(seed ^ 0x11)};
        entry.callsite_hash = CallsiteHash{mix(seed ^ 0x22)};
        entry.num_inputs = static_cast<uint16_t>(1 + (seed % 3));
        entry.num_outputs = 1;
        entry.num_scalar_args = static_cast<uint16_t>(seed % 3);
        entry.grad_enabled = ((seed & 0x100) != 0);

        auto* metas = arena.alloc_array<TensorMeta>(ctx.alloc, entry.num_inputs);
        for (uint16_t input_index = 0; input_index < entry.num_inputs; input_index++) {
            TensorMeta& meta = metas[input_index];
            meta = TensorMeta{};
            meta.ndim = 4;
            meta.dtype = ScalarType::Float;
            meta.device_type = DeviceType::CUDA;
            meta.device_idx = 0;
            for (uint8_t dim = 0; dim < 4; dim++) {
                const uint64_t dim_seed = mix(seed ^ (uint64_t{input_index} << 8) ^ (uint64_t{dim} << 16));
                meta.sizes[dim] = crucible::tensor_dim(static_cast<int64_t>(8 + (dim_seed % 256)));
                meta.strides[dim] = crucible::tensor_dim(static_cast<int64_t>(1 + (dim_seed % 64)));
            }
            // The address differs between the two builds, on purpose.
            const std::uintptr_t address = data_ptr_base + (uint64_t{op_index} * 64 + input_index) * 4096;
            meta.data_ptr = crucible::external_data_ptr(std::bit_cast<void*>(address));
        }
        entry.input_metas = metas;

        if (entry.num_scalar_args > 0) {
            auto* scalars = arena.alloc_array<int64_t>(ctx.alloc, entry.num_scalar_args);
            for (uint16_t scalar_index = 0; scalar_index < entry.num_scalar_args; scalar_index++) {
                scalars[scalar_index] = static_cast<int64_t>(mix(seed ^ (uint64_t{scalar_index} << 32)) % 4096);
            }
            entry.scalar_args = scalars;
        }
    }
}

void build_slots(TensorSlot* slots) noexcept {
    for (uint32_t slot_index = 0; slot_index < kNumSlots; slot_index++) {
        const uint64_t seed = mix(0x5107ULL ^ slot_index);
        const uint32_t birth = static_cast<uint32_t>(seed % 32);
        slots[slot_index] = TensorSlot{.offset_bytes = 0,
                                       .nbytes = 256 + (seed % 8) * 512,
                                       .birth_op = OpIndex{birth},
                                       .death_op = OpIndex{birth + 1 + static_cast<uint32_t>((seed >> 8) % 24)},
                                       .dtype = ScalarType::Float,
                                       .device_type = DeviceType::CPU,
                                       .device_idx = 0,
                                       .layout = Layout::Strided,
                                       .is_external = ((seed & 0x10000) != 0),
                                       .pad = {},
                                       .slot_id = SlotId{slot_index},
                                       .pad2 = {}};
    }
}

struct Built {
    uint64_t head_content_hash = 0;
    uint64_t tail_content_hash = 0;
    uint64_t merkle_hash = 0;
    uint64_t pool_bytes = 0;
    uint32_t num_slots = 0;
    uint32_t num_external = 0;
    uint64_t offsets[kNumSlots]{};
};

// TWO regions, chained.  For a lone region compute_merkle_hash returns the
// content hash unchanged — correct for a leaf, and it makes a merkle pin on
// one region a second copy of the content pin rather than a second pin.
// Chaining makes the fold do its work (fmix64 of the head's content against
// the tail's merkle hash), which is the O(1) subtree equality §L7 rests on,
// and main() checks that the two values actually came out different so this
// cannot silently decay back into one pin.
[[nodiscard]] Built build_once(crucible::effects::Test ctx, uint64_t data_ptr_base) noexcept {
    Arena arena(1 << 20);
    crucible::BackgroundThread bt;

    auto* ops = arena.alloc_array<TraceEntry>(ctx.alloc, kNumOps);
    build_ops(ctx, arena, ops, data_ptr_base);

    constexpr uint32_t kSplit = kNumOps / 2;
    auto* head = crucible::make_region(ctx.alloc, arena, ops, kSplit);
    auto* tail = crucible::make_region(ctx.alloc, arena, ops + kSplit, kNumOps - kSplit);
    head->next = tail;

    TensorSlot slots[kNumSlots]{};
    build_slots(slots);
    head->plan = bt.compute_memory_plan(ctx.alloc, slots, kNumSlots);
    crucible::recompute_merkle(head);

    Built out;
    out.head_content_hash = head->content_hash.raw();
    out.tail_content_hash = tail->content_hash.raw();
    out.merkle_hash = head->merkle_hash.raw();
    if (head->plan != nullptr) {
        out.pool_bytes = head->plan->pool_bytes;
        out.num_slots = head->plan->num_slots;
        out.num_external = head->plan->num_external;
        for (uint32_t slot_index = 0; slot_index < out.num_slots && slot_index < kNumSlots; slot_index++) {
            out.offsets[slot_index] = head->plan->slots[slot_index].offset_bytes;
        }
    }
    return out;
}

// The committed reference.  Printed by this test when it disagrees, so a
// deliberate format change can be read off a failing run rather than
// recomputed by hand.
constexpr uint64_t kReferenceHeadContentHash = 0x3a0a495d6cb7456cULL;
constexpr uint64_t kReferenceTailContentHash = 0x2b13c26aae81678fULL;
constexpr uint64_t kReferenceMerkleHash = 0xbd2ec96379ec78fbULL;
constexpr uint64_t kReferencePoolBytes = 12544;

}  // namespace

int main() {
    auto ctx = crucible::effects::testing::test();

    // Two different address bases, two arenas, second built after the first
    // so it cannot land where the first did.
    const Built first = build_once(ctx, 0x7F0000000000ULL);
    const Built second = build_once(ctx, 0x2A0000000000ULL);

    // Leg 1: address independence.
    if (first.head_content_hash != second.head_content_hash
        || first.tail_content_hash != second.tail_content_hash) {
        std::fprintf(stderr,
                     "content hash depends on where the tensors live: 0x%016llx vs 0x%016llx.\n"
                     "The two builds describe the same computation and differ only in data_ptr, so a "
                     "difference here means an address reached the content fold and content addressing "
                     "is broken: two identical computations would miss each other in the KernelCache.\n",
                     static_cast<unsigned long long>(first.head_content_hash),
                     static_cast<unsigned long long>(second.head_content_hash));
        return 2;
    }
    if (first.merkle_hash != second.merkle_hash) {
        std::fprintf(stderr, "merkle hash depends on address: 0x%016llx vs 0x%016llx\n",
                     static_cast<unsigned long long>(first.merkle_hash),
                     static_cast<unsigned long long>(second.merkle_hash));
        return 3;
    }

    // Leg 3, first half: the plan is a function of the slots alone.
    if (first.pool_bytes != second.pool_bytes || first.num_slots != second.num_slots
        || first.num_external != second.num_external) {
        std::fprintf(stderr,
                     "memory plan differs between two builds of the same slot set: pool %llu vs %llu, "
                     "slots %u vs %u, external %u vs %u\n",
                     static_cast<unsigned long long>(first.pool_bytes),
                     static_cast<unsigned long long>(second.pool_bytes), first.num_slots, second.num_slots,
                     first.num_external, second.num_external);
        return 4;
    }
    for (uint32_t slot_index = 0; slot_index < first.num_slots && slot_index < kNumSlots; slot_index++) {
        if (first.offsets[slot_index] != second.offsets[slot_index]) {
            std::fprintf(stderr,
                         "slot %u lands at a different offset on the second build: %llu vs %llu.\n"
                         "§L3 promises the same DFG produces the same addresses, so a kernel would see "
                         "different alignment on two runs of one program.\n",
                         slot_index, static_cast<unsigned long long>(first.offsets[slot_index]),
                         static_cast<unsigned long long>(second.offsets[slot_index]));
            return 5;
        }
    }

    // A hash of zero is what a default-constructed node carries, so it can
    // never be a real answer, and it would make the pin below vacuous.
    if (first.head_content_hash == 0 || first.tail_content_hash == 0 || first.merkle_hash == 0) {
        std::fprintf(stderr, "a content or merkle hash is zero — the pipeline produced no hash at all\n");
        return 6;
    }
    // The chain fold has to have folded.  If the merkle hash ever equals the
    // head's content hash again, the two regions stopped being linked and the
    // merkle pin below silently became a copy of the content pin.
    if (first.merkle_hash == first.head_content_hash) {
        std::fprintf(stderr,
                     "the merkle hash equals the head content hash, so the chain fold did nothing and the "
                     "merkle pin is not an independent pin\n");
        return 8;
    }

    // Leg 2: the reference pin.
    if (first.head_content_hash != kReferenceHeadContentHash
        || first.tail_content_hash != kReferenceTailContentHash || first.merkle_hash != kReferenceMerkleHash
        || first.pool_bytes != kReferencePoolBytes) {
        std::fprintf(stderr,
                     "the pipeline no longer produces the committed bytes.\n"
                     "  head content  expected 0x%016llx  measured 0x%016llx\n"
                     "  tail content  expected 0x%016llx  measured 0x%016llx\n"
                     "  merkle hash   expected 0x%016llx  measured 0x%016llx\n"
                     "  pool bytes    expected %llu  measured %llu\n"
                     "Both builds agree with each other, so this is not hidden state: the format changed. "
                     "If that change was deliberate, update these three constants in the same commit and "
                     "say in the message what moved, because every federation cache entry keyed on the old "
                     "value is now stale. If it was not deliberate, this is the DetSafe break §XIII says to "
                     "stop for.\n",
                     static_cast<unsigned long long>(kReferenceHeadContentHash),
                     static_cast<unsigned long long>(first.head_content_hash),
                     static_cast<unsigned long long>(kReferenceTailContentHash),
                     static_cast<unsigned long long>(first.tail_content_hash),
                     static_cast<unsigned long long>(kReferenceMerkleHash),
                     static_cast<unsigned long long>(first.merkle_hash),
                     static_cast<unsigned long long>(kReferencePoolBytes),
                     static_cast<unsigned long long>(first.pool_bytes));
        return 7;
    }

    std::fprintf(stderr,
                 "OK: %u ops in two chained regions and %u slots reproduce across two arenas and two address "
                 "bases, and match the committed head content hash 0x%016llx, merkle hash 0x%016llx and pool "
                 "size %llu bytes\n",
                 kNumOps, kNumSlots, static_cast<unsigned long long>(first.head_content_hash),
                 static_cast<unsigned long long>(first.merkle_hash),
                 static_cast<unsigned long long>(first.pool_bytes));
    return 0;
}
