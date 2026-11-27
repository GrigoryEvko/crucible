#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::DagArena;

// A drift in this hash means the wire format changed, which requires a
// CDAG_VERSION bump.
static uint64_t fnv1a_bytes(::fixy::View<const std::byte> bytes) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV offset basis
    for (const std::byte byte : bytes) {
        h ^= static_cast<uint8_t>(byte);
        h *= 0x100000001b3ULL;  // FNV prime
    }
    return h;
}

static crucible::TensorMeta make_meta(int64_t size0, int64_t size1 = 0) {
    crucible::TensorMeta m{};
    if (size1 > 0) {
        const ::fixy::FixedArray<crucible::TensorDim, 2> dims{::crucible::tensor_dim(size0),
                                                              ::crucible::tensor_dim(size1)};
        m.set_shape(crucible::contiguous_tensor_shape(crucible::TensorSizes{dims.view()})
                        .expect("a contiguous shape of rank 2"));
    } else {
        const ::fixy::FixedArray<crucible::TensorDim, 1> dims{::crucible::tensor_dim(size0)};
        m.set_shape(crucible::contiguous_tensor_shape(crucible::TensorSizes{dims.view()})
                        .expect("a contiguous shape of rank 1"));
    }
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = -1;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(
        std::bit_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF)));  // must become null on reload
    m.grad_fn_hash = crucible::grad_fn_hash(0xA11CE000BADDF00DULL);
    return m;
}

// The reason of a load that refuses its image.  A load that gives a node
// ends the test.
template <class Loaded>
[[nodiscard]] static crucible::LoadError reason_of(Loaded const& loaded) {
    return loaded.err().expect("the loader refuses the image");
}

// Count slots of the default value in the arena of the frame, each with its
// position as its id, as the build of a region gives them.  The loader refuses
// a slot whose id is not its position.
template <std::uint32_t Count, class Fresh>
[[nodiscard]] static crucible::DagFrameRun<crucible::TensorSlot, Fresh>
make_slots_in_place(::foundation::effects::Alloc alloc, crucible::DagFrame<Fresh>& frame) {
    auto slots_run = frame.template array<crucible::TensorSlot>(alloc, Count, crucible::TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<crucible::TensorSlot> slots = slots_run.view();
    std::uint32_t position = 0;
    for (crucible::TensorSlot& slot : slots) {
        slot.slot_id = crucible::SlotId{position};
        ++position;
    }
    return slots_run;
}

// The count of the ops of the region of the round trip.
constexpr uint32_t NUM_OPS = 3;
// A storage of more than 4 GiB, which a size of 32 bits cannot hold.
constexpr uint64_t kLargeStorageBytes = 98000000000ULL;

// The region of the round trip in the arena of the frame: NUM_OPS ops, each
// with inputs, outputs and one scalar.
template <class Fresh>
[[nodiscard]] static crucible::DagFrameRef<crucible::RegionNode, Fresh>
build_round_trip_region(::foundation::effects::Alloc alloc, crucible::DagFrame<Fresh>& frame) {
    auto ops_run = crucible::make_op_run(alloc, frame, NUM_OPS);
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<crucible::TraceEntry> built = ops_run.view();

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        crucible::TraceEntry& op = *built.get(i).expect("i is below NUM_OPS");
        op.schema_hash = crucible::SchemaHash{0xAA + i * 0x11};
        op.shape_hash = crucible::ShapeHash{0x100 + i};
        op.scope_hash = crucible::ScopeHash{0x200 + i};
        op.callsite_hash = crucible::CallsiteHash{0x300 + i};
        op.grad_enabled = (i % 2 == 0);
        // Vary op_flags across ops to exercise bit-packing round-trip.
        op.inference_mode = (i == 0);
        op.is_mutable = (i == 1);
        op.training_phase = static_cast<crucible::TrainingPhase>(i % 4);
        op.torch_function = (i == 2);
        // Ops 0 and 1 run inside op 2, and op 0 inside op 1.
        op.depth = crucible::OpDepth{static_cast<uint16_t>(NUM_OPS - 1 - i)};
        // The one scalar of op i has the tag i: an integer, a double, a
        // bool.  The double of op 1 is a data scalar.
        const crucible::DataSlots data =
            crucible::DataSlots{}.with(crucible::ScalarSlot{::fixy::index<0>()},
                                       i == 1 ? crucible::ScalarRole::DATA : crucible::ScalarRole::STRUCTURAL);
        op.scalar_kinds = crucible::ScalarSlotKinds::of(static_cast<uint16_t>(i), data);

        crucible::TensorMeta large_input = make_meta(4, 8);
        large_input.storage_nbytes = kLargeStorageBytes;
        const auto holder = crucible::hold_op(ops_run, i);
        crucible::TraceEntry::set_inputs(
            frame, holder, ::fixy::check::run_of(kTest, alloc, frame, large_input, make_meta(4, 8)),
            ::fixy::check::run_of(kTest, alloc, frame, (i > 0) ? crucible::OpIndex{i - 1} : crucible::OpIndex{},
                                  crucible::OpIndex{}),
            ::fixy::check::run_of(kTest, alloc, frame, (i > 0) ? crucible::SlotId{(i - 1) * 10} : crucible::SlotId{},
                                  crucible::SlotId{}));
        crucible::TraceEntry::set_outputs(frame, holder, ::fixy::check::run_of(kTest, alloc, frame, make_meta(4, 8)),
                                          ::fixy::check::run_of(kTest, alloc, frame, crucible::SlotId{i * 10}));
        crucible::TraceEntry::set_scalar_args(frame, holder,
                                              ::fixy::check::run_of(kTest, alloc, frame, static_cast<int64_t>(i * 42)));
    }
    return crucible::make_region(alloc, frame, ops_run);
}

// The region of the round trip with a plan that make_plan gives in the frame,
// in one frame on the arena.
template <class MakePlan>
[[nodiscard]] static crucible::DagRef<crucible::RegionNode> build_planned_region(::foundation::effects::Alloc alloc,
                                                                                 ::fixy::BumpArena<DagArena>& arena,
                                                                                 MakePlan const& make_plan) {
    return ::foundation::core::with_arena_frame(arena, [alloc, &make_plan](auto& frame) noexcept {
        const auto made = build_round_trip_region(alloc, frame);
        frame.store(made, &crucible::RegionNode::plan, make_plan(frame));
        return made.weaken();
    });
}

// The image of a region.
[[nodiscard]] static auto image_of(::foundation::effects::Alloc alloc, crucible::DagRef<crucible::RegionNode> region) {
    const crucible::DagRef<crucible::RegionNode const> region_reader = region;
    return ::fixy::with_mut(::fixy::mint_heap_array(alloc, 65536, std::byte{0}), [&region_reader](auto run) noexcept {
        return crucible::serialize_region(::fixy::to(*region_reader), std::move(run));
    });
}

// The load of the image of a region of one op with schema on its inputs
// inputs.  The writer writes each hash and each count as the op holds it, so
// the image can hold an op that no entry of the ring gives.  The load gives
// the region, or the reason of its refusal.
[[nodiscard]] static ::fixy::Result<::fixy::Unit, crucible::LoadError>
load_one_op(::foundation::effects::Alloc alloc, crucible::SchemaHash schema, ::fixy::ElementCount inputs) {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
    const crucible::DagRef<crucible::RegionNode const> reader =
        ::foundation::core::with_arena_frame(arena, [alloc, schema, inputs](auto& frame) noexcept {
            auto ops_run = crucible::make_op_run(alloc, frame, 1);
            const auto op = crucible::hold_op(ops_run, 0);
            op->schema_hash = schema;
            crucible::TraceEntry::set_inputs(
                frame, op, frame.template array<crucible::TensorMeta>(alloc, inputs.raw(), make_meta(4, 8)),
                frame.template array<crucible::OpIndex>(alloc, inputs.raw(), crucible::OpIndex{}),
                frame.template array<crucible::SlotId>(alloc, inputs.raw(), crucible::SlotId{}));
            crucible::TraceEntry::set_outputs(frame, op, ::fixy::check::run_of(kTest, alloc, frame, make_meta(4, 8)),
                                              ::fixy::check::run_of(kTest, alloc, frame, crucible::SlotId{}));
            return crucible::make_region(alloc, frame, ops_run).weaken();
        });
    auto [n, buf] =
        ::fixy::with_mut(::fixy::mint_heap_array(alloc, size_t{1} << 20, std::byte{0}), [&reader](auto run) noexcept {
            return crucible::serialize_region(::fixy::to(*reader), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, n > 0 && "the writer writes the image of one op");
    ::fixy::BumpArena<DagArena> load_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
    using Loaded = ::fixy::Result<::fixy::Unit, crucible::LoadError>;
    return crucible::deserialize_region(alloc, buf.view().window(0, n).expect("the writer writes inside its image"),
                                        load_arena)
        .match([](auto /*region*/) noexcept { return Loaded{::fixy::Unit{}}; },
               [](crucible::LoadError reason) noexcept { return Loaded{::fixy::err(reason)}; });
}

// A region holds only ops that the ring recorded, so the load of a region
// refuses an op that the one validator of an entry refuses: a schema hash of
// zero, and more inputs than kMaxOpInputs.  The old load took each of the two
// images, with a schema hash of zero and with 65 inputs, because its limits
// were 1024 inputs and no test of the hash.  An op of kMaxOpInputs inputs and
// a schema hash other than zero loads.
static void check_op_past_the_ring_is_refused() {
    const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
    constexpr crucible::SchemaHash kSchema{0x5C4E3A};
    CRUCIBLE_TEST_ASSERT(kTest, load_one_op(alloc, kSchema, ::fixy::ElementCount{crucible::kMaxOpInputs}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, load_one_op(alloc, crucible::SchemaHash{0}, ::fixy::ElementCount{1}).err()
                                    == ::fixy::some(crucible::LoadError::field_out_of_range));
    CRUCIBLE_TEST_ASSERT(kTest, load_one_op(alloc, kSchema, ::fixy::ElementCount{crucible::kMaxOpInputs + 1u}).err()
                                    == ::fixy::some(crucible::LoadError::count_above_limit));
}

// A copy of image in new storage, with the byte at at set to value.
[[nodiscard]] static ::fixy::HeapArray<std::byte>
with_byte(::foundation::effects::Alloc alloc, ::fixy::View<const std::byte> image, size_t at, std::byte value) {
    ::fixy::HeapArray<std::byte> copy = ::fixy::mint_heap_array_copy<std::byte>(alloc, image);
    copy.set(at, value);
    return copy;
}

// write_meta writes the image of a TensorMeta, and read_meta decodes those
// bytes as an UncheckedTensorMeta.  So the image of a descriptor and the
// image of its fields are one byte string, and decode_tensor_meta of the
// fields gives the descriptor back with no data pointer and no grad_fn.
static void check_descriptor_images_agree() {
    crucible::TensorMeta meta = make_meta(16, 4);
    meta.dtype = crucible::ScalarType::Half;
    meta.device_type = crucible::DeviceType::CUDA;
    meta.device_idx = 3;
    meta.layout = crucible::Layout::Strided;
    meta.requires_grad = true;
    meta.set_flags(crucible::meta_flags::IS_LEAF | crucible::meta_flags::IS_CONJ);
    meta.output_nr = 2;
    meta.storage_offset = -5;
    meta.version = 0x01020304u;
    meta.storage_nbytes = 0x1122334455667788ULL;

    crucible::UncheckedTensorMeta fields{};
    for (const auto lane : decltype(fields.sizes)::indices()) {
        const int64_t size = crucible::raw_tensor_dim(meta.sizes[lane.raw()]);
        const int64_t stride = crucible::raw_tensor_dim(meta.strides[lane.raw()]);
        fields.sizes.set(lane, size);
        fields.strides.set(lane, stride);
    }
    fields.ndim = meta.ndim;
    fields.dtype = std::to_underlying(meta.dtype);
    fields.device_type = std::to_underlying(meta.device_type);
    fields.device_idx = meta.device_idx;
    fields.layout = std::to_underlying(meta.layout);
    fields.requires_grad = static_cast<uint8_t>(meta.requires_grad ? 1 : 0);
    fields.flags = meta.flags;
    fields.output_nr = meta.output_nr;
    fields.storage_offset = meta.storage_offset;
    fields.version = meta.version;
    fields.storage_nbytes = meta.storage_nbytes;
    // The local parts of the fields have no place in the image.
    fields.pad0 = 0xFFFFFFFFu;
    fields.grad_fn_hash = 0x5EED;

    auto const& meta_read = meta;
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::TensorMeta>> const meta_image =
        ::fixy::build_fixed_array<std::byte, ::fixy::encoded_size_v<crucible::TensorMeta>>(
            [&meta_read](auto run) noexcept { ::fixy::encode(meta_read, std::move(run)); });
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::UncheckedTensorMeta>> const fields_image =
        ::fixy::build_fixed_array<std::byte, ::fixy::encoded_size_v<crucible::UncheckedTensorMeta>>(
            [&fields](auto run) noexcept { ::fixy::encode(fields, std::move(run)); });
    CRUCIBLE_TEST_ASSERT(kTest, meta_image == fields_image && "the image of a descriptor is the image of its fields");

    // The reader decodes the image, so the local parts of the fields come
    // back as zero.
    const auto& written = meta_image;
    const crucible::UncheckedTensorMeta read =
        ::fixy::decode<crucible::UncheckedTensorMeta>(written.view())
            .match([](crucible::UncheckedTensorMeta value) noexcept { return value; },
                   [](::fixy::DecodeFault) noexcept { return crucible::UncheckedTensorMeta{.ndim = 0xFF}; });
    CRUCIBLE_TEST_ASSERT(kTest, read.pad0 == 0 && read.grad_fn_hash == 0 && "the image holds no local part");
    const crucible::TensorMeta back =
        crucible::decode_tensor_meta(read, crucible::external_data_ptr(nullptr)).expect("the image holds a descriptor");
    crucible::TensorMeta expected = meta;
    expected.data_ptr = crucible::external_data_ptr(nullptr);
    expected.grad_fn_hash = crucible::grad_fn_hash(0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         back == expected && "the door gives the descriptor back with no data pointer and no grad_fn");
}

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    check_descriptor_images_agree();
    check_op_past_the_ring_is_refused();

    // One frame on the arena builds the ops, their arrays and the region.
    const crucible::DagRef<crucible::RegionNode> region =
        ::foundation::core::with_arena_frame(dag_arena, [alloc = test.alloc](auto& frame) noexcept {
            return build_round_trip_region(alloc, frame).weaken();
        });
    // The ops of the region, for the compare with the loaded ops.
    const ::fixy::View<const crucible::TraceEntry> ops = region->ops.view();
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS);
    // The bodies of the writer read the region through a Ref of const, so a
    // body reaches no part of the region that it can write.
    const crucible::DagRef<crucible::RegionNode const> reader = region;

    const crucible::ContentHash original_content_hash = region->content_hash;
    const crucible::MerkleHash original_merkle_hash = region->merkle_hash;
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(original_content_hash));

    auto [n, buf] =
        ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 65536, std::byte{0}), [&reader](auto run) noexcept {
            return crucible::serialize_region(::fixy::to(*reader), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, n > 0 && "serialize_region returned 0 — buffer too small or bad region");
    const ::fixy::View<const std::byte> wire = buf.view().window(0, n).expect("the writer writes inside its image");

    ::fixy::BumpArena<DagArena> arena2 = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const crucible::RegionNode& loaded =
        *crucible::deserialize_region(test.alloc, wire, arena2).expect("deserialize_region returns the region").value();

    CRUCIBLE_TEST_ASSERT(kTest, loaded.content_hash == original_content_hash);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.merkle_hash == original_merkle_hash);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.num_ops() == NUM_OPS);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.kind == crucible::TraceNodeKind::REGION);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        const crucible::TraceEntry& loaded_op = loaded.op_at(i);
        const crucible::TraceEntry& source_op = *ops.get(i).expect("i is below NUM_OPS");
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.schema_hash == source_op.schema_hash);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.shape_hash == source_op.shape_hash);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.scope_hash == source_op.scope_hash);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.num_inputs() == source_op.num_inputs());
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.num_outputs() == source_op.num_outputs());
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.num_scalar_args() == source_op.num_scalar_args());

        // These five fields share one packed byte on the wire.
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.grad_enabled == source_op.grad_enabled);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.inference_mode == source_op.inference_mode);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.is_mutable == source_op.is_mutable);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.training_phase == source_op.training_phase);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.torch_function == source_op.torch_function);
        CRUCIBLE_TEST_ASSERT(kTest,
                             loaded_op.depth == source_op.depth && "the store keeps the nesting depth of each op");
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.scalar_kinds == source_op.scalar_kinds
                                        && "the store keeps the tag and the role of each scalar");

        for (uint16_t j = 0; j < source_op.num_inputs(); j++) {
            const crucible::TensorMeta& loaded_meta =
                *loaded_op.input_metas().get(j).expect("a test reads an element at or past the count of its View");
            const crucible::TensorMeta& source_meta =
                *source_op.input_metas().get(j).expect("a test reads an element at or past the count of its View");
            CRUCIBLE_TEST_ASSERT(kTest, loaded_meta.data_ptr.value() == nullptr
                                            && "data_ptr must be null after deserialization");
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_grad_fn_hash(source_meta) != 0
                                            && "test fixture must exercise non-zero Family-B input");
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_grad_fn_hash(loaded_meta) == 0
                                            && "grad_fn_hash must be zero after deserialization");
            CRUCIBLE_TEST_ASSERT(kTest, loaded_meta.ndim == source_meta.ndim);
            CRUCIBLE_TEST_ASSERT(kTest, loaded_meta.dtype == source_meta.dtype);
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_tensor_dim(loaded_meta.sizes[0])
                                            == crucible::raw_tensor_dim(source_meta.sizes[0]));
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_tensor_dim(loaded_meta.sizes[1])
                                            == crucible::raw_tensor_dim(source_meta.sizes[1]));
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_tensor_dim(loaded_meta.strides[0])
                                            == crucible::raw_tensor_dim(source_meta.strides[0]));
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_tensor_dim(loaded_meta.strides[1])
                                            == crucible::raw_tensor_dim(source_meta.strides[1]));
        }
        for (uint16_t j = 0; j < source_op.num_outputs(); j++) {
            const crucible::TensorMeta& loaded_meta =
                *loaded_op.output_metas().get(j).expect("a test reads an element at or past the count of its View");
            const crucible::TensorMeta& source_meta =
                *source_op.output_metas().get(j).expect("a test reads an element at or past the count of its View");
            CRUCIBLE_TEST_ASSERT(kTest, loaded_meta.data_ptr.value() == nullptr);
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_grad_fn_hash(source_meta) != 0);
            CRUCIBLE_TEST_ASSERT(kTest, crucible::raw_grad_fn_hash(loaded_meta) == 0);
        }

        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.input_metas()
                                                .get(0)
                                                .expect("a test reads an element at or past the count of its View")
                                                ->storage_nbytes
                                            == kLargeStorageBytes
                                        && "a storage size above 4 GiB must keep each of its 64 bits");

        CRUCIBLE_TEST_ASSERT(
            kTest,
            *loaded_op.scalar_args().get(0).expect("a test reads an element at or past the count of its View")
                == *source_op.scalar_args().get(0).expect("a test reads an element at or past the count of its View"));

        CRUCIBLE_TEST_ASSERT(kTest, *loaded_op.input_trace_indices().get(0).expect(
                                        "a test reads an element at or past the count of its View")
                                        == *source_op.input_trace_indices().get(0).expect(
                                            "a test reads an element at or past the count of its View"));
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::equal(loaded_op.input_slot_ids(), source_op.input_slot_ids()));
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::equal(loaded_op.output_slot_ids(), source_op.output_slot_ids()));
    }

    const crucible::ContentHash recomputed = crucible::compute_content_hash(loaded.ops.view());
    CRUCIBLE_TEST_ASSERT(kTest, recomputed == original_content_hash);

    // Catches an uninitialised byte reaching the wire, which the
    // structural round-trip above cannot see.  Each call writes a
    // separately poisoned buffer, so neither can bias the other.
    {
        auto [na, buf_a] =
            ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 65536, std::byte{0xAA}), [&reader](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(*reader), std::move(run));
            });
        auto [nb, buf_b] =
            ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 65536, std::byte{0xBB}), [&reader](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(*reader), std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest, na == nb && "serialize_region byte count must be deterministic");
        CRUCIBLE_TEST_ASSERT(kTest, na > 0);
        CRUCIBLE_TEST_ASSERT(kTest,
                             ::fixy::equal(buf_a.view().window(0, na).expect("the writer writes inside its image"),
                                           buf_b.view().window(0, nb).expect("the writer writes inside its image"))
                                 && "serialize_region bytes must be deterministic — uninit-memory leak?");
    }

    // Wire bytes are cross-process stable by construction: data_ptr is
    // zeroed on write, padding is explicit, and strong-typed fields go
    // out as raw bytes.  Any drift here is a wire-format change and
    // needs a CDAG_VERSION bump in the same commit as the golden.
    //
    // The byte length catches size drift, the hash catches content
    // drift at a stable size.
    {
        static constexpr uint32_t EXPECTED_CDAG_VERSION = 20;
        static_assert(crucible::CDAG_VERSION.value() == EXPECTED_CDAG_VERSION,
                      "CDAG_VERSION bump detected — update wire-byte golden below "
                      "after confirming the new bytes hash to the expected value.");

        const uint64_t wire_hash = fnv1a_bytes(wire);

        // CDAG_VERSION 11 writes the storage size of each meta in 8 bytes,
        // not 4.  CDAG_VERSION 12 writes the nesting depth of each op in 2
        // bytes, and CDAG_VERSION 13 the tags of its scalars in 2 more.  The
        // image holds 3 ops, so it is 12 bytes longer than in 11.
        // CDAG_VERSION 14 writes the numerics hash of the region in 8 bytes,
        // zero for this region of make_region.  The content hash of the
        // header folds the tags, the depths, the producers and the outputs of
        // the ops.  CDAG_VERSION 15 gives the shape hash of each op the
        // meaning of its guard word, so only the version moves the bytes.
        // CDAG_VERSION 16 folds the rank, the layout, the two bits and the
        // storage offset of each tensor into its word.  The tensors of this
        // region have the ranks 2 and 1, so the version and the content hash
        // of the header move the bytes.  CDAG_VERSION 17 writes the data
        // slots of each op in the word of its tags, and the double of op 1 is
        // a data scalar.  So the version, the word of op 1 and the content
        // hash of the header move the bytes.  CDAG_VERSION 18 writes each
        // tensor descriptor as its image of the Protocol family, with no data
        // pointer and no hash of the grad_fn: 16 bytes less for each of the 9
        // descriptors of the image, so 144 bytes less.  CDAG_VERSION 19 ends
        // the image with the 32 bytes of the checksum of each byte before it.
        // CDAG_VERSION 20 takes the element type of a tensor as one byte.  The
        // tensors of this region are Float, so only the version word and the
        // checksum move the bytes.
        constexpr size_t EXPECTED_WIRE_BYTES = 1716;
        constexpr uint64_t EXPECTED_WIRE_HASH = 0xda0fde7e7e034713ULL;

        if (n != EXPECTED_WIRE_BYTES || wire_hash != EXPECTED_WIRE_HASH) {
            ::fixy::report(::fixy::Sink::Err,
                           "WIRE-FORMAT DRIFT DETECTED\n"
                           "  got    n={}  hash=0x{}\n"
                           "  expect n={}  hash=0x{}\n"
                           "  if intentional: update EXPECTED_WIRE_BYTES and\n"
                           "  EXPECTED_WIRE_HASH in test_serialize.cpp, and\n"
                           "  verify CDAG_VERSION was bumped in Serialize.h.\n",
                           n, ::fixy::Hex<16>{wire_hash}, EXPECTED_WIRE_BYTES, ::fixy::Hex<16>{EXPECTED_WIRE_HASH});
            CRUCIBLE_TEST_ASSERT(kTest, false && "wire-byte golden mismatch");
        }
    }

    auto [n_tiny, tiny_buf] =
        ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 4, std::byte{0}), [&reader](auto run) noexcept {
            return crucible::serialize_region(::fixy::to(*reader), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    ::fixy::BumpArena<DagArena> arena3 = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    auto bad_region =
        crucible::deserialize_region(test.alloc, wire.window(0, 10).expect("the image holds ten bytes"), arena3);
    CRUCIBLE_TEST_ASSERT(kTest, reason_of(bad_region) == crucible::LoadError::truncated
                                    && "a cut header must refuse as truncated");

    // Each fault of an image gives its own reason, and an image with two
    // faults gives the reason of the first.  The offsets: 0..3 the magic,
    // 4..7 the version, 8 the kind, 9..15 the pad, 24..31 the content hash,
    // 32..35 the count of ops, 36..43 the schema of the first op.
    {
        ::fixy::BumpArena<DagArena> reason_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        // The byte at at with its low bit changed, so it differs from the
        // byte of the image.
        const auto flipped_at = [&wire](size_t at) {
            const uint8_t value = static_cast<uint8_t>(*wire.get(at).expect("the byte is inside the image"));
            return std::byte{static_cast<uint8_t>(value ^ 1u)};
        };
        const auto reason_with_byte = [&](size_t at, std::byte value) {
            const ::fixy::HeapArray<std::byte> damaged = with_byte(test.alloc, wire, at, value);
            return reason_of(crucible::deserialize_region(test.alloc, damaged.view(), reason_arena));
        };
        using crucible::LoadError;
        const ::fixy::View<const std::byte> cut = wire.window(0, n - 1).expect("the image holds its last byte");
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(test.alloc, cut, reason_arena))
                                        == LoadError::truncated);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(0, flipped_at(0)) == LoadError::bad_magic);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(4, flipped_at(4)) == LoadError::version_mismatch);
        CRUCIBLE_TEST_ASSERT(kTest,
                             reason_with_byte(8, std::byte{static_cast<uint8_t>(crucible::TraceNodeKind::BRANCH)})
                                 == LoadError::wrong_kind);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(8, std::byte{0xFF}) == LoadError::field_out_of_range);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(9, std::byte{1}) == LoadError::field_out_of_range);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(24, flipped_at(24)) == LoadError::content_hash_mismatch);
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(36, flipped_at(36)) == LoadError::first_schema_mismatch);

        // The ops start at 61, after the fields of the region and its plan
        // flag.  Op 0 holds its count of scalars at 97..98 and the word of
        // its tags and data slots at 104..105.  Op 0 holds one scalar, so the
        // bit 11 of the word is the data bit of slot 1, past the count.
        CRUCIBLE_TEST_ASSERT(kTest, static_cast<uint8_t>(*wire.get(97).expect("the byte is inside the image")) == 1
                                        && "op 0 holds one scalar");
        CRUCIBLE_TEST_ASSERT(kTest, reason_with_byte(105, std::byte{0x08}) == LoadError::field_out_of_range
                                        && "a data slot past the count is not an image of the writer");

        // A bad magic number and a bad kind byte: the magic comes first.
        const ::fixy::HeapArray<std::byte> bad_kind = with_byte(test.alloc, wire, 8, std::byte{0xFF});
        const ::fixy::HeapArray<std::byte> two_faults = with_byte(test.alloc, bad_kind.view(), 0, std::byte{0});
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(test.alloc, two_faults.view(), reason_arena))
                                        == LoadError::bad_magic);

        // The loader of a branch refuses the image of a region by its kind.
        const LoadError branch_reason =
            ::foundation::core::with_arena_frame(reason_arena, [alloc = test.alloc, &wire](auto& frame) noexcept {
                return reason_of(crucible::deserialize_branch(alloc, wire, frame));
            });
        CRUCIBLE_TEST_ASSERT(kTest, branch_reason == LoadError::wrong_kind);
    }

    // The image of a branch whose arms are out of the order of their values,
    // and the same image with a count of arms above its ceiling.  The count
    // is at 44..47, after the header and the 12 bytes of the guard.
    {
        ::fixy::BumpArena<DagArena> branch_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
        ::foundation::core::with_arena_frame(branch_arena, [alloc = test.alloc](auto& frame) noexcept {
            auto const branch = crucible::mint_dag_node<crucible::BranchNode>(alloc, frame);
            branch->kind = crucible::TraceNodeKind::BRANCH;
            // The arm at position 0 holds the value 2, and the arm at
            // position 1 the value 1.
            auto arms = frame.template build_array<crucible::BranchNode::Arm>(
                alloc, ::fixy::ElementCount{2},
                [](::fixy::RunStart at) noexcept { return crucible::BranchNode::Arm{at.value().raw() == 0 ? 2 : 1}; });
            frame.store(branch, &crucible::BranchNode::arm_run, arms);
            crucible::BranchNode const& branch_read = *branch;
            auto [branch_bytes, branch_buf] =
                ::fixy::with_mut(::fixy::mint_heap_array(alloc, 256, std::byte{0}), [&branch_read](auto run) noexcept {
                    return crucible::serialize_branch(::fixy::to(branch_read), std::move(run));
                });
            CRUCIBLE_TEST_ASSERT(kTest, branch_bytes > 0);
            const ::fixy::View<const std::byte> branch_image =
                branch_buf.view().window(0, branch_bytes).expect("the writer writes inside its image");
            CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_branch(alloc, branch_image, frame))
                                            == crucible::LoadError::arms_unsorted);
            const ::fixy::HeapArray<std::byte> many_arms = with_byte(alloc, branch_image, 47, std::byte{0xFF});
            CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_branch(alloc, many_arms.view(), frame))
                                            == crucible::LoadError::count_above_limit);
            // The loader of a region refuses the image of a branch by its
            // kind, in an arena of its own.
            ::fixy::BumpArena<DagArena> region_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
            CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(alloc, branch_image, region_arena))
                                            == crucible::LoadError::wrong_kind);
        });
    }

    // A header claiming 0xFFFFFFFF ops would otherwise drive an
    // allocation of four billion entries before anything failed.
    {
        // Offset 0..3 = magic; 4..7 = version; 8 = kind byte; 9..15 pad;
        // 16..23 merkle; 24..31 content; 32..35 num_ops.
        // The writer holds the ViewMut of the array, so the writer stays in
        // the frame of the array.
        const ::fixy::HeapArray<std::byte> adv =
            ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 128, std::byte{0}), [](auto run) noexcept {
                using Run = decltype(run);
                crucible::detail_ser::Writer<typename Run::brand_type, Run::extent> adv_writer{.image = std::move(run)};
                // merkle_hash + content_hash stay zero.
                crucible::detail_ser::write_header(adv_writer, crucible::TraceNodeKind::REGION, crucible::MerkleHash{},
                                                   crucible::ContentHash{});
                const uint32_t bogus_num_ops = 0xFFFFFFFFu;
                adv_writer.w(bogus_num_ops);
                CRUCIBLE_TEST_ASSERT(kTest, adv_writer.has_room && adv_writer.pos == 36);
            });

        ::fixy::BumpArena<DagArena> arena4 = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto adversarial_region = crucible::deserialize_region(test.alloc, adv.view(), arena4);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(adversarial_region) == crucible::LoadError::count_above_limit
                                        && "deserialize_region must reject num_ops > CDAG_MAX_OPS");
    }

    // A loaded slot_id indexes a slot table sized to the plan's
    // num_slots.  The replay hot path checks only validity, so an
    // out-of-range slot_id would read past that table.  This region's
    // output slot ids are {0, 10, 20}, so num_slots 21 admits all of
    // them and num_slots 3 rejects two.  A plan-less region has no pool
    // to bound against and is exempt.
    {
        ::fixy::BumpArena<DagArena> ok_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::DagRef<crucible::RegionNode> ok_region =
            build_planned_region(test.alloc, ok_arena, [alloc = test.alloc](auto& frame) noexcept {
                const auto ok_plan = frame.template make<crucible::MemoryPlan>(alloc);
                frame.store(ok_plan, &crucible::MemoryPlan::slots, make_slots_in_place<21>(alloc, frame));
                return ok_plan;
            });
        auto [pn, pbuf] = image_of(test.alloc, ok_region);
        CRUCIBLE_TEST_ASSERT(kTest, pn > 0 && "plan-bearing serialize failed");
        ::fixy::BumpArena<DagArena> parena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto ok_loaded = crucible::deserialize_region(
            test.alloc, pbuf.view().window(0, pn).expect("the writer writes inside its image"), parena);
        CRUCIBLE_TEST_ASSERT(kTest, ok_loaded.is_ok() && "plan-bearing region with in-range slot_ids must load");

        ::fixy::BumpArena<DagArena> bad_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::DagRef<crucible::RegionNode> out_of_range_region =
            build_planned_region(test.alloc, bad_arena, [alloc = test.alloc](auto& frame) noexcept {
                const auto bad_plan = frame.template make<crucible::MemoryPlan>(alloc);
                frame.store(bad_plan, &crucible::MemoryPlan::slots, make_slots_in_place<3>(alloc, frame));
                return bad_plan;
            });
        auto [nn, nbuf] = image_of(test.alloc, out_of_range_region);
        CRUCIBLE_TEST_ASSERT(kTest, nn > 0 && "plan-bearing (oob) serialize failed");
        ::fixy::BumpArena<DagArena> narena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto bad_loaded = crucible::deserialize_region(
            test.alloc, nbuf.view().window(0, nn).expect("the writer writes inside its image"), narena);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(bad_loaded) == crucible::LoadError::slot_out_of_range
                                        && "deserialize_region must reject slot_id >= plan->num_slots");
    }

    // The ops name a slot by its position, and the replay registers each
    // external slot under its position.  So the loader refuses a plan with a
    // slot whose id is not its position: two slots with swapped ids, and a
    // slot with the default id.  The plan has 21 slots, so the bound of the
    // slot ids of the ops holds.
    {
        // The reason of the load of the image of a region whose plan holds 21
        // slots, of which the two at first and second take the ids first_id
        // and second_id.
        const auto reason_of_slots = [&test](uint32_t first, crucible::SlotId first_id, uint32_t second,
                                             crucible::SlotId second_id) {
            ::fixy::BumpArena<DagArena> plan_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
            const crucible::DagRef<crucible::RegionNode> placed_region = build_planned_region(
                test.alloc, plan_arena, [alloc = test.alloc, first, first_id, second, second_id](auto& frame) noexcept {
                    auto slots = make_slots_in_place<21>(alloc, frame);
                    slots.view().get(first).expect("the plan holds 21 slots")->slot_id = first_id;
                    slots.view().get(second).expect("the plan holds 21 slots")->slot_id = second_id;
                    const auto placed_plan = frame.template make<crucible::MemoryPlan>(alloc);
                    frame.store(placed_plan, &crucible::MemoryPlan::slots, slots);
                    return placed_plan;
                });
            auto [placed_bytes, placed_buf] = image_of(test.alloc, placed_region);
            CRUCIBLE_TEST_ASSERT(kTest, placed_bytes > 0 && "the serialize of a plan of 21 slots fits");
            ::fixy::BumpArena<DagArena> placed_load = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
            return reason_of(crucible::deserialize_region(
                test.alloc, placed_buf.view().window(0, placed_bytes).expect("the writer writes inside its image"),
                placed_load));
        };
        CRUCIBLE_TEST_ASSERT(kTest, reason_of_slots(4, crucible::SlotId{5}, 5, crucible::SlotId{4})
                                            == crucible::LoadError::slot_out_of_place
                                        && "deserialize_region must refuse two slots with swapped ids");
        CRUCIBLE_TEST_ASSERT(kTest, reason_of_slots(7, crucible::SlotId{}, 8, crucible::SlotId{8})
                                            == crucible::LoadError::slot_out_of_place
                                        && "deserialize_region must refuse a slot with the default id");
    }

    // An out-of-range pool_bytes becomes an [[assume]] under the release
    // contract semantic, so the load boundary must reject it against the
    // same constant the allocator checks.  num_slots is 21 so the
    // slot-id gate cannot fire, leaving pool_bytes the only rejector.
    // The bound is inclusive.
    {
        // A region whose plan of 21 slots has the pool size pool_bytes.
        const auto region_of_pool = [&test](::fixy::BumpArena<DagArena>& pool_arena, uint64_t pool_bytes) {
            return build_planned_region(test.alloc, pool_arena, [alloc = test.alloc, pool_bytes](auto& frame) noexcept {
                const auto pool_plan = frame.template make<crucible::MemoryPlan>(alloc);
                frame.store(pool_plan, &crucible::MemoryPlan::slots, make_slots_in_place<21>(alloc, frame));
                pool_plan->pool_bytes = pool_bytes;
                return pool_plan;
            });
        };
        ::fixy::BumpArena<DagArena> over_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto [on, obuf] = image_of(test.alloc, region_of_pool(over_arena, crucible::PoolAllocator::kMaxPoolBytes + 1));
        CRUCIBLE_TEST_ASSERT(kTest, on > 0 && "over-pool_bytes serialize failed");
        ::fixy::BumpArena<DagArena> oarena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto over_loaded = crucible::deserialize_region(
            test.alloc, obuf.view().window(0, on).expect("the writer writes inside its image"), oarena);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(over_loaded) == crucible::LoadError::plan_unservable
                                        && "deserialize_region must reject pool_bytes > kMaxPoolBytes");

        ::fixy::BumpArena<DagArena> at_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto [an, abuf] = image_of(test.alloc, region_of_pool(at_arena, crucible::PoolAllocator::kMaxPoolBytes));
        CRUCIBLE_TEST_ASSERT(kTest, an > 0 && "at-cap pool_bytes serialize failed");
        ::fixy::BumpArena<DagArena> aarena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto at_loaded = crucible::deserialize_region(
            test.alloc, abuf.view().window(0, an).expect("the writer writes inside its image"), aarena);
        CRUCIBLE_TEST_ASSERT(kTest, at_loaded.is_ok() && "pool_bytes == kMaxPoolBytes must be accepted (inclusive)");
    }

    // A writer sets every pad byte of a slot to zero, so one non-zero byte
    // in a pad refuses the whole image.  The id of a slot is its position, so
    // the last slot carries a death op that occurs once in the image, which
    // locates its pads.  The death op is the four bytes at 20 of the slot,
    // the 3-byte pad is at 29, the id at 32, and the 4-byte pad2 at 36.
    {
        constexpr uint32_t kMarkerDeathOp = 0x5A5A5A5Au;
        constexpr size_t kPadAfterDeathOp = 29 - 20;
        constexpr size_t kPad2AfterDeathOp = 36 - 20;
        ::fixy::BumpArena<DagArena> pad_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::DagRef<crucible::RegionNode> pad_region =
            build_planned_region(test.alloc, pad_arena, [alloc = test.alloc](auto& frame) noexcept {
                auto pad_slots = make_slots_in_place<21>(alloc, frame);
                pad_slots.view().get(20).expect("the pad plan holds 21 slots")->death_op =
                    crucible::OpIndex{kMarkerDeathOp};
                const auto pad_plan = frame.template make<crucible::MemoryPlan>(alloc);
                frame.store(pad_plan, &crucible::MemoryPlan::slots, pad_slots);
                return pad_plan;
            });
        auto [image_bytes, image_buf] = image_of(test.alloc, pad_region);
        CRUCIBLE_TEST_ASSERT(kTest, image_bytes > 0 && "pad-probe serialize failed");
        const ::fixy::View<const std::byte> image =
            image_buf.view().window(0, image_bytes).expect("the writer writes inside its image");

        // The death op at at, read as the writer writes a word: the low byte
        // first.
        const auto holds_marker_at = [&](size_t at) {
            return image.window<sizeof(kMarkerDeathOp)>(at).match(
                [](::fixy::View<const std::byte, sizeof(kMarkerDeathOp)> word) noexcept {
                    uint32_t value = 0;
                    for (const auto index : word.indices()) {
                        value |= static_cast<uint32_t>(word.at(index)) << (8 * index.raw());
                    }
                    return value == kMarkerDeathOp;
                },
                [] noexcept { return false; });
        };
        size_t marker_at = image_bytes;
        size_t marker_count = 0;
        for (size_t at = 0; at + sizeof(kMarkerDeathOp) <= image_bytes; ++at) {
            if (holds_marker_at(at)) {
                marker_at = at;
                ++marker_count;
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, marker_count == 1 && "the marker death op must occur once in the image");
        const size_t pad_at = marker_at + kPadAfterDeathOp;
        const size_t pad2_at = marker_at + kPad2AfterDeathOp;

        ::fixy::BumpArena<DagArena> clean_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto clean = crucible::deserialize_region(test.alloc, image, clean_arena);
        CRUCIBLE_TEST_ASSERT(kTest, clean.is_ok() && "the unmodified pad-probe image must load");

        const auto refuses_with_byte_set_at = [&](size_t at) {
            ::fixy::HeapArray<std::byte> corrupt = ::fixy::mint_heap_array_copy<std::byte>(test.alloc, image);
            corrupt.set(at, std::byte{1});
            ::fixy::BumpArena<DagArena> corrupt_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
            return reason_of(crucible::deserialize_region(test.alloc, corrupt.view(), corrupt_arena))
                == crucible::LoadError::field_out_of_range;
        };
        for (size_t byte = 0; byte < 4; ++byte) {
            CRUCIBLE_TEST_ASSERT(kTest, refuses_with_byte_set_at(pad2_at + byte)
                                            && "a non-zero byte in pad2 must refuse the image");
        }
        for (size_t byte = 0; byte < 3; ++byte) {
            CRUCIBLE_TEST_ASSERT(kTest, refuses_with_byte_set_at(pad_at + byte)
                                            && "a non-zero byte in the slot pad must refuse the image");
        }
    }

    // An op with scalar arguments and no tensors.  Each array of an entry
    // holds its count, so the image of such a region loads back, and the
    // loaded op holds the same scalars.
    {
        ::fixy::BumpArena<DagArena> scalar_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::DagRef<crucible::RegionNode> scalar_node =
            ::foundation::core::with_arena_frame(scalar_arena, [alloc = test.alloc](auto& frame) noexcept {
                auto scalar_ops_run = crucible::make_op_run(alloc, frame, 1);
                // A plain View of an arena run: the View carries no brand of its arena.
                const ::fixy::View<crucible::TraceEntry> scalar_ops = scalar_ops_run.view();
                crucible::TraceEntry& op = *scalar_ops.get(0).expect("the region holds one op");
                op.schema_hash = crucible::SchemaHash{0x77};
                crucible::TraceEntry::set_scalar_args(
                    frame, crucible::hold_op(scalar_ops_run, 0),
                    ::fixy::check::run_of(kTest, alloc, frame, int64_t{5}, int64_t{6}));
                return crucible::make_region(alloc, frame, scalar_ops_run).weaken();
            });
        const crucible::RegionNode& scalar_region = *scalar_node;
        const crucible::TraceEntry& scalar_op = scalar_region.op_at(0);

        auto [scalar_bytes, scalar_buf] = ::fixy::with_mut(
            ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0}), [&scalar_region](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(scalar_region), std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest,
                             scalar_bytes > 0 && "the region of an op with scalars and no tensors must serialize");
        ::fixy::BumpArena<DagArena> load_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::RegionNode& loaded_scalars =
            *crucible::deserialize_region(
                 test.alloc, scalar_buf.view().window(0, scalar_bytes).expect("the writer writes inside its image"),
                 load_arena)
                 .expect("the region of an op with scalars and no tensors must load back")
                 .value();
        const crucible::TraceEntry& loaded_op = loaded_scalars.op_at(0);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_op.num_inputs() == 0 && loaded_op.num_outputs() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::equal(loaded_op.scalar_args(), scalar_op.scalar_args()));
    }

    // A region of the background pipeline holds the numerics hash that its
    // content hash folds.  The image keeps the hash, the load folds the ops
    // under it, and an image whose hash names a different policy is refused.
    {
        ::fixy::BumpArena<DagArena> policy_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        crucible::NumericsPolicy strict{};
        strict.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
        const crucible::NumericsHash numerics = crucible::compute_numerics_hash(strict, crucible::EagerNumerics{});
        const crucible::DagRef<crucible::RegionNode> policy_node =
            ::foundation::core::with_arena_frame(policy_arena, [alloc = test.alloc, numerics](auto& frame) noexcept {
                auto policy_ops_run = crucible::make_op_run(alloc, frame, 1);
                // A plain View of an arena run: the View carries no brand of its arena.
                const ::fixy::View<crucible::TraceEntry> policy_ops = policy_ops_run.view();
                crucible::TraceEntry& policy_op = *policy_ops.get(0).expect("the region holds one op");
                policy_op.schema_hash = crucible::SchemaHash{0x78};
                crucible::TraceEntry::set_scalar_args(frame, crucible::hold_op(policy_ops_run, 0),
                                                      ::fixy::check::run_of(kTest, alloc, frame, int64_t{7}));
                auto const made = crucible::mint_dag_node<crucible::RegionNode>(alloc, frame);
                crucible::init_region_node(frame, made, policy_ops_run,
                                           crucible::compute_content_hash(policy_ops, numerics), numerics);
                return made.weaken();
            });
        crucible::RegionNode& policy_region = *policy_node;

        auto const& policy_reader = policy_region;
        auto [policy_bytes, policy_buf] = ::fixy::with_mut(
            ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0}), [&policy_reader](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(policy_reader), std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest, policy_bytes > 0);
        ::fixy::BumpArena<DagArena> policy_load = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::RegionNode& loaded_policy =
            *crucible::deserialize_region(
                 test.alloc, policy_buf.view().window(0, policy_bytes).expect("the writer writes inside its image"),
                 policy_load)
                 .expect("a region under a policy loads back")
                 .value();
        CRUCIBLE_TEST_ASSERT(kTest, loaded_policy.numerics == numerics
                                        && loaded_policy.content_hash == policy_region.content_hash);

        // The numerics hash follows the first schema, after the 32 bytes of
        // the header and the count of the ops.
        constexpr size_t kNumericsOffset = 32 + 4 + 8;
        const ::fixy::View<const std::byte> policy_image =
            policy_buf.view().window(0, policy_bytes).expect("the image fits");
        const crucible::NumericsHash ordered =
            crucible::compute_numerics_hash(crucible::NumericsPolicy{}, crucible::EagerNumerics{});
        const ::fixy::HeapArray<std::byte> other =
            ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, policy_bytes, std::byte{0}),
                             [policy_image, ordered](auto run) noexcept {
                                 run = ::fixy::reborrow(std::move(run), [policy_image](auto lent) noexcept {
                                     ::fixy::copy(std::move(lent), policy_image).expect("the two runs hold the image");
                                 });
                                 for (size_t byte = 0; byte < 8; ++byte) {
                                     auto element =
                                         run.get_mut(kNumericsOffset + byte).expect("the image holds the hash");
                                     *element = static_cast<std::byte>((ordered.raw() >> (8 * byte)) & 0xFF);
                                 }
                             });
        ::fixy::BumpArena<DagArena> other_load = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(test.alloc, other.view(), other_load))
                                            == crucible::LoadError::content_hash_mismatch
                                        && "the ops of the image do not hash to its content hash under another policy");
    }

    // A flipped bit in any byte of an image is refused.  The image holds a
    // plan of two slots that the pool allocator can serve.  So the flips reach
    // the merkle hash, the measured time, the variant, the pool size and the
    // offset and the size of each slot, which no fold of the ops covers.  The
    // old loader took most of those flips, and a replay of such a plan puts
    // two tensors on wrong or overlapping memory.
    {
        ::fixy::BumpArena<DagArena> flip_dag = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::DagRef<crucible::RegionNode> flip_node =
            ::foundation::core::with_arena_frame(flip_dag, [alloc = test.alloc](auto& frame) noexcept {
                auto flip_ops_run = crucible::make_op_run(alloc, frame, 1);
                // A plain View of an arena run: the View carries no brand of its arena.
                const ::fixy::View<crucible::TraceEntry> flip_ops = flip_ops_run.view();
                crucible::TraceEntry& flip_op = *flip_ops.get(0).expect("the region holds one op");
                flip_op.schema_hash = crucible::SchemaHash{0x79};
                const auto flip_holder = crucible::hold_op(flip_ops_run, 0);
                crucible::TraceEntry::set_inputs(frame, flip_holder,
                                                 ::fixy::check::run_of(kTest, alloc, frame, make_meta(16)),
                                                 ::fixy::check::run_of(kTest, alloc, frame, crucible::OpIndex{}),
                                                 ::fixy::check::run_of(kTest, alloc, frame, crucible::SlotId{0}));
                crucible::TraceEntry::set_outputs(frame, flip_holder,
                                                  ::fixy::check::run_of(kTest, alloc, frame, make_meta(16)),
                                                  ::fixy::check::run_of(kTest, alloc, frame, crucible::SlotId{1}));
                auto const made = crucible::make_region(alloc, frame, flip_ops_run);
                auto const flip_plan = frame.template make<crucible::MemoryPlan>(alloc);
                flip_plan->pool_bytes = 2 * crucible::PoolAllocator::ALIGNMENT;
                auto flip_slots_run = frame.template array<crucible::TensorSlot>(alloc, 2, crucible::TensorSlot{});
                // A plain View of an arena run: the View carries no brand of its arena.
                const ::fixy::View<crucible::TensorSlot> flip_slots = flip_slots_run.view();
                std::uint32_t slot_index = 0;
                for (crucible::TensorSlot& slot : flip_slots) {
                    slot.offset_bytes = slot_index * crucible::PoolAllocator::ALIGNMENT;
                    slot.nbytes = 64;
                    slot.dtype = crucible::ScalarType::Float;
                    slot.slot_id = crucible::SlotId{slot_index};
                    ++slot_index;
                }
                frame.store(flip_plan, &crucible::MemoryPlan::slots, flip_slots_run);
                frame.store(made, &crucible::RegionNode::plan, flip_plan);
                return made.weaken();
            });
        crucible::RegionNode& flip_region = *flip_node;

        auto const& flip_reader = flip_region;
        auto [flip_bytes, flip_buf] = ::fixy::with_mut(
            ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0}), [&flip_reader](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(flip_reader), std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest, flip_bytes > 0);
        const ::fixy::View<const std::byte> flip_image =
            flip_buf.view().window(0, flip_bytes).expect("the writer writes inside its image");
        ::fixy::BumpArena<DagArena> clean_load = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        CRUCIBLE_TEST_ASSERT(kTest, crucible::deserialize_region(test.alloc, flip_image, clean_load).is_ok()
                                        && "the image with a plan loads before a flip");
        size_t accepted_flips = 0;
        for (size_t at = 0; at < flip_bytes; ++at) {
            const uint8_t original = static_cast<uint8_t>(*flip_image.get(at).expect("the byte is inside the image"));
            for (uint32_t bit = 0; bit < 8; ++bit) {
                const ::fixy::HeapArray<std::byte> flipped =
                    with_byte(test.alloc, flip_image, at, std::byte{static_cast<uint8_t>(original ^ (1u << bit))});
                ::fixy::BumpArena<DagArena> flip_load = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
                if (crucible::deserialize_region(test.alloc, flipped.view(), flip_load).is_ok()) ++accepted_flips;
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, accepted_flips == 0 && "each flipped bit of an image is refused");
    }

    ::fixy::check::pass(kTest, "test_serialize: all tests passed\n");
    return 0;
}
