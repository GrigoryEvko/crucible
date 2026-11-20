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
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    op.set_inputs(::fixy::check::run_of(kTest, bg.alloc, dag_arena, in_meta),
                  ::fixy::check::run_of(kTest, bg.alloc, dag_arena, crucible::OpIndex{0}),
                  ::fixy::check::run_of(kTest, bg.alloc, dag_arena, crucible::SlotId{}));

    const ::fixy::Ref<crucible::RegionNode> region = crucible::make_region(bg.alloc, dag_arena, ops);
    return crucible::serialize_region(::fixy::to(*region), out_buf);
}

// The wire layout, little-endian, restated here as this test's ground
// truth.  A change that moves any field reddens this file, which is the
// point of writing the offsets out rather than computing them.
//
// Header: magic(4) version(4) kind(1) pad(7) merkle(8) content(8) = 32B.
constexpr size_t kKindOffset = 8;  // first byte after magic+version.

// Region payload after the 32B header:
//   num_ops(4) first_op_schema(8) numerics(8) measured_ms(4) variant_id(4)
//   has_plan(1)
constexpr size_t kRegionPrefix = 32 + 4 + 8 + 8 + 4 + 4 + 1;  // = 61
// TraceEntry header:
//   schema(8) shape(8) scope(8) callsite(8) num_inputs(2) num_outputs(2)
//   num_scalar_args(2) grad_enabled(1) op_flags(1) kernel_id(1) depth(2)
//   scalar_types(2)
constexpr size_t kEntryHeader = 8 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 1 + 2 + 2;  // = 45
// The image of a descriptor: sizes[8]*8 + strides[8]*8 + ndim(1) + dtype(1)
//   + device_type(1) + device_idx(1) + layout(1) + requires_grad(1)
//   + flags(1) + ... with no data pointer.
constexpr size_t kMetaBase = kRegionPrefix + kEntryHeader;  // first meta start
constexpr size_t kMetaSize0Top = kMetaBase + 7;  // the high byte of sizes[0]
constexpr size_t kMetaSize1 = kMetaBase + 8;  // the low byte of sizes[1], past the rank 1
constexpr size_t kMetaNdim = kMetaBase + 64 + 64;  // ndim byte
constexpr size_t kMetaDtype = kMetaNdim + 1;  // dtype byte
constexpr size_t kMetaDevice = kMetaDtype + 1;  // device_type byte
constexpr size_t kMetaDeviceIdx = kMetaDevice + 1;  // device_idx byte
constexpr size_t kMetaLayout = kMetaDeviceIdx + 1;  // layout byte
constexpr size_t kMetaGrad = kMetaLayout + 1;  // requires_grad byte
constexpr size_t kMetaFlags = kMetaGrad + 1;  // flags byte

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
        ::fixy::report(::fixy::Sink::Err, "serialize failed\n");
        return 1;
    }
    const ::fixy::View<const std::byte> image = buf.view().window(0, n).expect("the writer writes inside its image");

    // Untouched bytes must still round-trip: a gate that rejects
    // everything would pass every case below.
    if (is_rejected(bg, image)) {
        ::fixy::report(::fixy::Sink::Err, "happy path wrongly rejected — gate broke success path\n");
        return 2;
    }

    // For each trust-boundary enum field, corrupt exactly one byte to a
    // value the predicate rejects, and assert the malformed input is
    // rejected.  Each case copies the pristine bytes first.
    struct Corruption {
        ::fixy::TextView name;
        size_t off;
        uint8_t bad;
    };
    const ::fixy::FixedArray<Corruption, 10> cases{
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
        // A size of 0x70 in its high byte is far past kMaxTensorDimExtent.
        {"meta.sizes[0]", kMetaSize0Top, 0x70},
        // The rank is 1, so the lane of sizes[1] must hold zero.
        {"meta.sizes[1]", kMetaSize1, 1},
        // The host index is -1, and an index below it names no device.
        {"meta.device_idx", kMetaDeviceIdx, 0xFE},
        // A bool byte is 0 or 1.
        {"meta.requires_grad", kMetaGrad, 2},
        // Bits 6 and 7 of the flags are reserved.
        {"meta.flags", kMetaFlags, 0x40},
    };

    for (const auto& c : cases) {
        if (c.off >= n) {
            ::fixy::report(::fixy::Sink::Err, "offset for {} ({}) past wire size {} — layout drift\n", c.name, c.off,
                           n);
            return 3;
        }
        ::fixy::HeapArray<std::byte> corrupt = ::fixy::mint_heap_array(bg.alloc, n, std::byte{0});
        ::fixy::copy(corrupt.view(), image).expect("the copy has the count of the image");
        std::byte& target = *corrupt.get(c.off).expect("the offset is inside the image");
        // Confirm we are actually flipping the byte to a DIFFERENT value
        // (otherwise the test would pass vacuously).
        if (target == std::byte{c.bad}) {
            ::fixy::report(::fixy::Sink::Err, "{}: corrupt byte equals original — no mutation\n", c.name);
            return 4;
        }
        target = std::byte{c.bad};
        if (!is_rejected(bg, corrupt.view())) {
            ::fixy::report(::fixy::Sink::Err,
                           "{}: malformed byte {} at offset {} NOT rejected "
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

    ::fixy::check::pass(kTest, "OK: all trust-boundary enum gates reject malformed wire bytes\n");
    return 0;
}
