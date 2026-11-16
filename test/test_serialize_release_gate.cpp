// The deserialize boundary rebuilds enum fields out of untrusted wire
// bytes and wraps each in a refinement.  A refinement checks its predicate
// only while contracts are enforced.  With them stripped the clause becomes
// an assumption and no branch is emitted.  So the boundary carries a hard
// check of its own, ahead of each refinement, and a malformed byte comes
// back as a null region either way.
//
// This file is the witness for the stripped build, and it is compiled both
// ways.  Since the assert macro does nothing once assertions are off, none
// appears here: every check is an explicit branch with its own exit code.
// Exit zero means every malformed byte was rejected and the untouched
// bytes still round-trip.

#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "test_assert.h"

namespace {

// Returns the number of bytes written, or zero if the region could not be
// built.  The op, its arrays and the region are in an arena of the call.
[[nodiscard]] size_t build_and_serialize(::foundation::effects::Test bg, ::fixy::View<std::byte> out_buf) {
    ::fixy::BumpArena dag_arena{1 << 16};
    const ::fixy::View<crucible::TraceEntry> ops =
        dag_arena.array<crucible::TraceEntry>(bg.alloc, 1, crucible::TraceEntry{});
    crucible::TraceEntry& op = *ops.get(0).expect("the region holds one op");

    op.schema_hash = crucible::SchemaHash{0xAA};
    op.shape_hash = crucible::ShapeHash{0x100};
    op.scope_hash = crucible::ScopeHash{0x200};
    op.callsite_hash = crucible::CallsiteHash{0x300};
    op.grad_enabled = true;

    crucible::TensorMeta in_meta{};
    in_meta.ndim = 1;
    in_meta.sizes[0] = crucible::tensor_dim(8);
    in_meta.strides[0] = crucible::tensor_dim(1);
    in_meta.dtype = crucible::ScalarType::Float;
    in_meta.device_type = crucible::DeviceType::CPU;
    in_meta.device_idx = -1;
    in_meta.layout = crucible::Layout::Strided;
    op.set_inputs(crucible::test::run_of(bg.alloc, dag_arena, in_meta),
                  crucible::test::run_of(bg.alloc, dag_arena, crucible::OpIndex{0}),
                  crucible::test::run_of(bg.alloc, dag_arena, crucible::SlotId{}));

    const ::fixy::Ref<crucible::RegionNode> region = crucible::make_region(bg.alloc, dag_arena, ops);
    return crucible::serialize_region(crucible::SerializedRegion{*region}, out_buf);
}

// The wire layout, little-endian, restated here as this test's ground
// truth.  A change that moves any field reddens this file, which is the
// point of writing the offsets out rather than computing them.
//
// Header: magic(4) version(4) kind(1) pad(7) merkle(8) content(8) = 32B.
constexpr size_t kKindOffset = 8;  // first byte after magic+version.

// Region payload after the 32B header:
//   num_ops(4) first_op_schema(8) measured_ms(4) variant_id(4) has_plan(1)
constexpr size_t kRegionPrefix = 32 + 4 + 8 + 4 + 4 + 1;  // = 53
// TraceEntry header:
//   schema(8) shape(8) scope(8) callsite(8) num_inputs(2) num_outputs(2)
//   num_scalar_args(2) grad_enabled(1) op_flags(1) kernel_id(1) depth(2)
constexpr size_t kEntryHeader = 8 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 1 + 2;  // = 43
// write_meta layout: sizes[8]*8 + strides[8]*8 + data_ptr(8) + ndim(1)
//   + dtype(1) + ...
constexpr size_t kMetaBase = kRegionPrefix + kEntryHeader;  // first meta start
constexpr size_t kMetaNdim = kMetaBase + 64 + 64 + 8;  // ndim byte
constexpr size_t kMetaDtype = kMetaNdim + 1;  // dtype byte
constexpr size_t kMetaDevice = kMetaDtype + 1;  // device_type byte
constexpr size_t kMetaLayout = kMetaDevice + 1 + 1;  // +device_idx, layout byte

// Rejection is no region coming back out of the boundary.
[[nodiscard]] bool is_rejected(::foundation::effects::Test bg, ::fixy::View<const std::byte> bytes) {
    ::fixy::BumpArena arena{1 << 16};
    return crucible::deserialize_region(bg.alloc, bytes, arena).is_err();
}

// A rejection of a byte outside the values of its field names that reason.
[[nodiscard]] bool is_rejected_as_field_out_of_range(::foundation::effects::Test bg,
                                                     ::fixy::View<const std::byte> bytes) {
    ::fixy::BumpArena arena{1 << 16};
    return crucible::deserialize_region(bg.alloc, bytes, arena)
        .match([](crucible::LoadedRegionNode) noexcept { return false; },
               [](crucible::LoadError reason) noexcept { return reason == crucible::LoadError::field_out_of_range; });
}

}  // namespace

[[gnu::cold]] int main() {
    auto bg = ::foundation::effects::testing::test();

    ::fixy::HeapArray<std::byte> buf = ::fixy::mint_heap_array(bg.alloc, 65536, std::byte{0});
    const size_t n = build_and_serialize(bg, buf.view());
    if (n == 0) {
        std::fprintf(stderr, "serialize failed\n");
        return 1;
    }
    const ::fixy::View<const std::byte> image = buf.view().window(0, n).expect("the writer writes inside its image");

    // Untouched bytes must still round-trip: a gate that rejects
    // everything would pass every case below.
    if (is_rejected(bg, image)) {
        std::fprintf(stderr, "happy path wrongly rejected — gate broke success path\n");
        return 2;
    }

    // For each trust-boundary enum field, corrupt exactly one byte to a
    // value the predicate rejects, and assert the malformed input is
    // rejected.  Each case copies the pristine bytes first.
    struct Corruption {
        const char* name;
        size_t off;
        uint8_t bad;
    };
    const Corruption cases[] = {
        // The node kind runs to a terminal value, so 99 is far out of range.
        {"header.kind", kKindOffset, 99},
        // A tensor has at most 8 dimensions, so 200 is out of range.
        {"meta.ndim", kMetaNdim, 200},
        // The scalar-type enumeration has a hole at 14.
        {"meta.dtype", kMetaDtype, 14},
        // The device-type enumeration has a hole at 3.
        {"meta.device_type", kMetaDevice, 3},
        // The layout enumeration is dense up to 5, so 99 is out of range.
        {"meta.layout", kMetaLayout, 99},
    };

    for (const auto& c : cases) {
        if (c.off >= n) {
            std::fprintf(stderr, "offset for %s (%zu) past wire size %zu — layout drift\n", c.name, c.off, n);
            return 3;
        }
        ::fixy::HeapArray<std::byte> corrupt = ::fixy::mint_heap_array(bg.alloc, n, std::byte{0});
        ::fixy::copy(corrupt.view(), image).expect("the copy has the count of the image");
        std::byte& target = *corrupt.get(c.off).expect("the offset is inside the image");
        // Confirm we are actually flipping the byte to a DIFFERENT value
        // (otherwise the test would pass vacuously).
        if (target == std::byte{c.bad}) {
            std::fprintf(stderr, "%s: corrupt byte equals original — no mutation\n", c.name);
            return 4;
        }
        target = std::byte{c.bad};
        if (!is_rejected(bg, corrupt.view())) {
            std::fprintf(stderr,
                         "%s: malformed byte 0x%02x at offset %zu NOT rejected "
                         "(release-stripped contract let it through)\n",
                         c.name, c.bad, c.off);
            return 5;
        }
        if (!is_rejected_as_field_out_of_range(bg, corrupt.view())) {
            ::fixy::report(::fixy::Sink::Err,
                           "a malformed byte {} at offset {} is rejected with a reason other than "
                           "field_out_of_range\n",
                           c.bad, c.off);
            return 6;
        }
    }

    crucible::test::pass("OK: all trust-boundary enum gates reject malformed wire bytes\n");
    return 0;
}
