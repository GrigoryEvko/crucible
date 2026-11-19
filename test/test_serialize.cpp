#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <bit>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
        m.ndim = 2;
        m.sizes[0] = ::crucible::tensor_dim(size0);
        m.sizes[1] = ::crucible::tensor_dim(size1);
        m.strides[0] = ::crucible::tensor_dim(size1);
        m.strides[1] = ::crucible::tensor_dim(1);
    } else {
        m.ndim = 1;
        m.sizes[0] = ::crucible::tensor_dim(size0);
        m.strides[0] = ::crucible::tensor_dim(1);
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

// A copy of image in new storage, with the byte at at set to value.
[[nodiscard]] static ::fixy::HeapArray<std::byte>
with_byte(::foundation::effects::Alloc alloc, ::fixy::View<const std::byte> image, size_t at, std::byte value) {
    ::fixy::HeapArray<std::byte> copy = ::fixy::mint_heap_array(alloc, image.size(), std::byte{0});
    ::fixy::copy(copy.view(), image).expect("the copy has the count of the image");
    *copy.get(at).expect("the byte is inside the image") = value;
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
    meta.flags = crucible::meta_flags::IS_LEAF | crucible::meta_flags::IS_CONJ;
    meta.output_nr = 2;
    meta.storage_offset = -5;
    meta.version = 0x01020304u;
    meta.storage_nbytes = 0x1122334455667788ULL;

    crucible::UncheckedTensorMeta fields{};
    for (const auto lane : decltype(fields.sizes)::indices()) {
        fields.sizes.at(lane) = crucible::raw_tensor_dim(meta.sizes[lane.raw()]);
        fields.strides.at(lane) = crucible::raw_tensor_dim(meta.strides[lane.raw()]);
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

    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::TensorMeta>> meta_image{};
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::UncheckedTensorMeta>> fields_image{};
    ::fixy::encode(meta, meta_image.view());
    ::fixy::encode(fields, fields_image.view());
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
    ::fixy::BumpArena dag_arena{1 << 16};
    check_descriptor_images_agree();

    constexpr uint32_t NUM_OPS = 3;
    // A storage of more than 4 GiB, which a size of 32 bits cannot hold.
    constexpr uint64_t kLargeStorageBytes = 98000000000ULL;
    const ::fixy::View<crucible::TraceEntry> ops =
        dag_arena.array<crucible::TraceEntry>(test.alloc, NUM_OPS, crucible::TraceEntry{});

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        crucible::TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
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
        // The one scalar of op i has the tag i: an integer, a double, a bool.
        // The double of op 1 is a data scalar.
        const crucible::DataSlots data =
            crucible::DataSlots{}.with(crucible::ScalarSlot{::fixy::index<0>()},
                                       i == 1 ? crucible::ScalarRole::DATA : crucible::ScalarRole::STRUCTURAL);
        op.scalar_kinds = crucible::ScalarSlotKinds::of(static_cast<uint16_t>(i), data);

        crucible::TensorMeta large_input = make_meta(4, 8);
        large_input.storage_nbytes = kLargeStorageBytes;
        op.set_inputs(
            ::fixy::check::run_of(kTest, test.alloc, dag_arena, large_input, make_meta(4, 8)),
            ::fixy::check::run_of(kTest, test.alloc, dag_arena,
                                  (i > 0) ? crucible::OpIndex{i - 1} : crucible::OpIndex{}, crucible::OpIndex{}),
            ::fixy::check::run_of(kTest, test.alloc, dag_arena,
                                  (i > 0) ? crucible::SlotId{(i - 1) * 10} : crucible::SlotId{}, crucible::SlotId{}));
        op.set_outputs(::fixy::check::run_of(kTest, test.alloc, dag_arena, make_meta(4, 8)),
                       ::fixy::check::run_of(kTest, test.alloc, dag_arena, crucible::SlotId{i * 10}));
        op.set_scalar_args(::fixy::check::run_of(kTest, test.alloc, dag_arena, static_cast<int64_t>(i * 42)));
    }

    const ::fixy::Ref<crucible::RegionNode> region = crucible::make_region(test.alloc, dag_arena, ops);
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS);

    const crucible::ContentHash original_content_hash = region->content_hash;
    const crucible::MerkleHash original_merkle_hash = region->merkle_hash;
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(original_content_hash));

    ::fixy::HeapArray<std::byte> buf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
    const size_t n = crucible::serialize_region(crucible::SerializedRegion{*region}, buf.view());
    CRUCIBLE_TEST_ASSERT(kTest, n > 0 && "serialize_region returned 0 — buffer too small or bad region");
    const ::fixy::View<const std::byte> wire = buf.view().window(0, n).expect("the writer writes inside its image");

    ::fixy::BumpArena arena2{1 << 16};
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

    const crucible::ContentHash recomputed = crucible::compute_content_hash(loaded.ops);
    CRUCIBLE_TEST_ASSERT(kTest, recomputed == original_content_hash);

    // Catches an uninitialised byte reaching the wire, which the
    // structural round-trip above cannot see.  Each call writes a
    // separately poisoned buffer, so neither can bias the other.
    {
        ::fixy::HeapArray<std::byte> buf_a = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0xAA});
        ::fixy::HeapArray<std::byte> buf_b = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0xBB});

        const size_t na = crucible::serialize_region(crucible::SerializedRegion{*region}, buf_a.view());
        const size_t nb = crucible::serialize_region(crucible::SerializedRegion{*region}, buf_b.view());
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
        static constexpr uint32_t EXPECTED_CDAG_VERSION = 18;
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
        // descriptors of the image, so 144 bytes less.
        constexpr size_t EXPECTED_WIRE_BYTES = 1684;
        constexpr uint64_t EXPECTED_WIRE_HASH = 0xaa19bc806f609735ULL;

        if (n != EXPECTED_WIRE_BYTES || wire_hash != EXPECTED_WIRE_HASH) {
            std::fprintf(stderr,
                         "WIRE-FORMAT DRIFT DETECTED\n"
                         "  got    n=%zu  hash=0x%016" PRIx64 "\n"
                         "  expect n=%zu  hash=0x%016" PRIx64 "\n"
                         "  if intentional: update EXPECTED_WIRE_BYTES and\n"
                         "  EXPECTED_WIRE_HASH in test_serialize.cpp, and\n"
                         "  verify CDAG_VERSION was bumped in Serialize.h.\n",
                         n, wire_hash, EXPECTED_WIRE_BYTES, EXPECTED_WIRE_HASH);
            CRUCIBLE_TEST_ASSERT(kTest, false && "wire-byte golden mismatch");
        }
    }

    ::fixy::HeapArray<std::byte> tiny_buf = ::fixy::mint_heap_array(test.alloc, 4, std::byte{0});
    const size_t n_tiny = crucible::serialize_region(crucible::SerializedRegion{*region}, tiny_buf.view());
    CRUCIBLE_TEST_ASSERT(kTest, n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    ::fixy::BumpArena arena3{1 << 16};
    auto bad_region =
        crucible::deserialize_region(test.alloc, wire.window(0, 10).expect("the image holds ten bytes"), arena3);
    CRUCIBLE_TEST_ASSERT(kTest, reason_of(bad_region) == crucible::LoadError::truncated
                                    && "a cut header must refuse as truncated");

    // Each fault of an image gives its own reason, and an image with two
    // faults gives the reason of the first.  The offsets: 0..3 the magic,
    // 4..7 the version, 8 the kind, 9..15 the pad, 24..31 the content hash,
    // 32..35 the count of ops, 36..43 the schema of the first op.
    {
        ::fixy::BumpArena reason_arena{1 << 16};
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
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_branch(test.alloc, wire, reason_arena))
                                        == LoadError::wrong_kind);
    }

    // The image of a branch whose arms are out of the order of their values,
    // and the same image with a count of arms above its ceiling.  The count
    // is at 44..47, after the header and the 12 bytes of the guard.
    {
        ::fixy::BumpArena branch_arena{1 << 12};
        crucible::BranchNode& branch = *branch_arena.make<crucible::BranchNode>(test.alloc);
        branch.kind = crucible::TraceNodeKind::BRANCH;
        branch.set_arms(branch_arena.array<crucible::BranchNode::Arm>(test.alloc, 2, crucible::BranchNode::Arm{}));
        branch.arms().get(0).expect("the branch holds two arms")->value = 2;
        branch.arms().get(1).expect("the branch holds two arms")->value = 1;
        ::fixy::HeapArray<std::byte> branch_buf = ::fixy::mint_heap_array(test.alloc, 256, std::byte{0});
        const size_t branch_bytes = crucible::serialize_branch(crucible::SerializedBranch{branch}, branch_buf.view());
        CRUCIBLE_TEST_ASSERT(kTest, branch_bytes > 0);
        const ::fixy::View<const std::byte> branch_image =
            branch_buf.view().window(0, branch_bytes).expect("the writer writes inside its image");
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_branch(test.alloc, branch_image, branch_arena))
                                        == crucible::LoadError::arms_unsorted);
        const ::fixy::HeapArray<std::byte> many_arms = with_byte(test.alloc, branch_image, 47, std::byte{0xFF});
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_branch(test.alloc, many_arms.view(), branch_arena))
                                        == crucible::LoadError::count_above_limit);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(test.alloc, branch_image, branch_arena))
                                        == crucible::LoadError::wrong_kind);
    }

    // A header claiming 0xFFFFFFFF ops would otherwise drive an
    // allocation of four billion entries before anything failed.
    {
        // Offset 0..3 = magic; 4..7 = version; 8 = kind byte; 9..15 pad;
        // 16..23 merkle; 24..31 content; 32..35 num_ops.
        ::fixy::HeapArray<std::byte> adv = ::fixy::mint_heap_array(test.alloc, 128, std::byte{0});
        crucible::detail_ser::Writer adv_writer{.image = adv.view()};
        // merkle_hash + content_hash stay zero.
        crucible::detail_ser::write_header(adv_writer, crucible::TraceNodeKind::REGION, crucible::MerkleHash{},
                                           crucible::ContentHash{});
        const uint32_t bogus_num_ops = 0xFFFFFFFFu;
        adv_writer.w(bogus_num_ops);
        CRUCIBLE_TEST_ASSERT(kTest, adv_writer.has_room && adv_writer.pos == 36);

        ::fixy::BumpArena arena4{1 << 16};
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
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan ok_plan{};
        ok_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        region->plan = crucible::make_plan_link(ok_plan);

        ::fixy::HeapArray<std::byte> pbuf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
        const size_t pn = crucible::serialize_region(crucible::SerializedRegion{*region}, pbuf.view());
        CRUCIBLE_TEST_ASSERT(kTest, pn > 0 && "plan-bearing serialize failed");
        ::fixy::BumpArena parena{1 << 16};
        auto ok_loaded = crucible::deserialize_region(
            test.alloc, pbuf.view().window(0, pn).expect("the writer writes inside its image"), parena);
        CRUCIBLE_TEST_ASSERT(kTest, ok_loaded.is_ok() && "plan-bearing region with in-range slot_ids must load");

        crucible::MemoryPlan bad_plan{};
        bad_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 3, crucible::TensorSlot{});
        region->plan = crucible::make_plan_link(bad_plan);

        ::fixy::HeapArray<std::byte> nbuf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
        const size_t nn = crucible::serialize_region(crucible::SerializedRegion{*region}, nbuf.view());
        CRUCIBLE_TEST_ASSERT(kTest, nn > 0 && "plan-bearing (oob) serialize failed");
        ::fixy::BumpArena narena{1 << 16};
        auto bad_loaded = crucible::deserialize_region(
            test.alloc, nbuf.view().window(0, nn).expect("the writer writes inside its image"), narena);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(bad_loaded) == crucible::LoadError::slot_out_of_range
                                        && "deserialize_region must reject slot_id >= plan->num_slots");

        region->plan = ::fixy::none;  // nothing later depends on this.
    }

    // An out-of-range pool_bytes becomes an [[assume]] under the release
    // contract semantic, so the load boundary must reject it against the
    // same constant the allocator checks.  num_slots is 21 so the
    // slot-id gate cannot fire, leaving pool_bytes the only rejector.
    // The bound is inclusive.
    {
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan over_plan{};
        over_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        over_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes + 1;
        region->plan = crucible::make_plan_link(over_plan);

        ::fixy::HeapArray<std::byte> obuf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
        const size_t on = crucible::serialize_region(crucible::SerializedRegion{*region}, obuf.view());
        CRUCIBLE_TEST_ASSERT(kTest, on > 0 && "over-pool_bytes serialize failed");
        ::fixy::BumpArena oarena{1 << 16};
        auto over_loaded = crucible::deserialize_region(
            test.alloc, obuf.view().window(0, on).expect("the writer writes inside its image"), oarena);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(over_loaded) == crucible::LoadError::plan_unservable
                                        && "deserialize_region must reject pool_bytes > kMaxPoolBytes");

        crucible::MemoryPlan at_plan{};
        at_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        at_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes;
        region->plan = crucible::make_plan_link(at_plan);

        ::fixy::HeapArray<std::byte> abuf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
        const size_t an = crucible::serialize_region(crucible::SerializedRegion{*region}, abuf.view());
        CRUCIBLE_TEST_ASSERT(kTest, an > 0 && "at-cap pool_bytes serialize failed");
        ::fixy::BumpArena aarena{1 << 16};
        auto at_loaded = crucible::deserialize_region(
            test.alloc, abuf.view().window(0, an).expect("the writer writes inside its image"), aarena);
        CRUCIBLE_TEST_ASSERT(kTest, at_loaded.is_ok() && "pool_bytes == kMaxPoolBytes must be accepted (inclusive)");

        region->plan = ::fixy::none;  // restore.
    }

    // A writer sets every pad byte of a slot to zero, so one non-zero byte
    // in a pad refuses the whole image.  The last slot carries a slot id
    // that occurs once in the image, which locates its pads: the 3-byte
    // pad ends at the slot id, and the 4-byte pad2 starts after it.
    {
        constexpr uint32_t kMarkerSlotId = 0x5A5A5A5Au;
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan pad_plan{};
        const ::fixy::View<crucible::TensorSlot> pad_slots =
            slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        pad_slots.get(20).expect("the pad plan holds 21 slots")->slot_id = crucible::SlotId{kMarkerSlotId};
        pad_plan.slots = pad_slots;
        region->plan = crucible::make_plan_link(pad_plan);

        ::fixy::HeapArray<std::byte> image_buf = ::fixy::mint_heap_array(test.alloc, 65536, std::byte{0});
        const size_t image_bytes = crucible::serialize_region(crucible::SerializedRegion{*region}, image_buf.view());
        CRUCIBLE_TEST_ASSERT(kTest, image_bytes > 0 && "pad-probe serialize failed");
        const ::fixy::View<const std::byte> image =
            image_buf.view().window(0, image_bytes).expect("the writer writes inside its image");

        // The slot id at at, read as the writer writes a word: the low byte
        // first.
        const auto holds_marker_at = [&](size_t at) {
            return image.window<sizeof(kMarkerSlotId)>(at).match(
                [](::fixy::View<const std::byte, sizeof(kMarkerSlotId)> word) noexcept {
                    uint32_t value = 0;
                    for (const auto index : word.indices()) {
                        value |= static_cast<uint32_t>(word.at(index)) << (8 * index.raw());
                    }
                    return value == kMarkerSlotId;
                },
                [] noexcept { return false; });
        };
        size_t marker_at = image_bytes;
        size_t marker_count = 0;
        for (size_t at = 0; at + sizeof(kMarkerSlotId) <= image_bytes; ++at) {
            if (holds_marker_at(at)) {
                marker_at = at;
                ++marker_count;
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, marker_count == 1 && "the marker slot id must occur once in the image");
        const size_t pad_at = marker_at - 3;
        const size_t pad2_at = marker_at + sizeof(kMarkerSlotId);

        ::fixy::BumpArena clean_arena{1 << 16};
        auto clean = crucible::deserialize_region(test.alloc, image, clean_arena);
        CRUCIBLE_TEST_ASSERT(kTest, clean.is_ok() && "the unmodified pad-probe image must load");

        const auto refuses_with_byte_set_at = [&](size_t at) {
            ::fixy::HeapArray<std::byte> corrupt = ::fixy::mint_heap_array(test.alloc, image_bytes, std::byte{0});
            ::fixy::copy(corrupt.view(), image).expect("the copy has the count of the image");
            *corrupt.get(at).expect("the pad is inside the image") = std::byte{1};
            ::fixy::BumpArena corrupt_arena{1 << 16};
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

        region->plan = ::fixy::none;
    }

    // An op with scalar arguments and no tensors.  Each array of an entry
    // holds its count, so the image of such a region loads back, and the
    // loaded op holds the same scalars.
    {
        ::fixy::BumpArena scalar_arena{1 << 16};
        const ::fixy::View<crucible::TraceEntry> scalar_ops =
            scalar_arena.array<crucible::TraceEntry>(test.alloc, 1, crucible::TraceEntry{});
        crucible::TraceEntry& scalar_op = *scalar_ops.get(0).expect("the region holds one op");
        scalar_op.schema_hash = crucible::SchemaHash{0x77};
        scalar_op.set_scalar_args(::fixy::check::run_of(kTest, test.alloc, scalar_arena, int64_t{5}, int64_t{6}));
        const crucible::RegionNode& scalar_region = *crucible::make_region(test.alloc, scalar_arena, scalar_ops);

        ::fixy::HeapArray<std::byte> scalar_buf = ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0});
        const size_t scalar_bytes =
            crucible::serialize_region(crucible::SerializedRegion{scalar_region}, scalar_buf.view());
        CRUCIBLE_TEST_ASSERT(kTest,
                             scalar_bytes > 0 && "the region of an op with scalars and no tensors must serialize");
        ::fixy::BumpArena load_arena{1 << 16};
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
        ::fixy::BumpArena policy_arena{1 << 16};
        const ::fixy::View<crucible::TraceEntry> policy_ops =
            policy_arena.array<crucible::TraceEntry>(test.alloc, 1, crucible::TraceEntry{});
        crucible::TraceEntry& policy_op = *policy_ops.get(0).expect("the region holds one op");
        policy_op.schema_hash = crucible::SchemaHash{0x78};
        policy_op.set_scalar_args(::fixy::check::run_of(kTest, test.alloc, policy_arena, int64_t{7}));
        crucible::NumericsPolicy strict{};
        strict.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
        const crucible::NumericsHash numerics = crucible::compute_numerics_hash(strict, crucible::EagerNumerics{});
        crucible::RegionNode& policy_region = *policy_arena.make<crucible::RegionNode>(test.alloc);
        crucible::init_region_node(policy_region, policy_ops, crucible::compute_content_hash(policy_ops, numerics),
                                   numerics);

        ::fixy::HeapArray<std::byte> policy_buf = ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0});
        const size_t policy_bytes =
            crucible::serialize_region(crucible::SerializedRegion{policy_region}, policy_buf.view());
        CRUCIBLE_TEST_ASSERT(kTest, policy_bytes > 0);
        ::fixy::BumpArena policy_load{1 << 16};
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
        ::fixy::HeapArray<std::byte> other = ::fixy::mint_heap_array(test.alloc, policy_bytes, std::byte{0});
        ::fixy::copy(other.view(), policy_buf.view().window(0, policy_bytes).expect("the image fits"))
            .expect("the two runs hold the image");
        const crucible::NumericsHash ordered =
            crucible::compute_numerics_hash(crucible::NumericsPolicy{}, crucible::EagerNumerics{});
        for (size_t byte = 0; byte < 8; ++byte) {
            *other.view().get(kNumericsOffset + byte).expect("the image holds the hash") =
                static_cast<std::byte>((ordered.raw() >> (8 * byte)) & 0xFF);
        }
        ::fixy::BumpArena other_load{1 << 16};
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(crucible::deserialize_region(test.alloc, other.view(), other_load))
                                            == crucible::LoadError::content_hash_mismatch
                                        && "the ops of the image do not hash to its content hash under another policy");
    }

    ::fixy::check::pass(kTest, "test_serialize: all tests passed\n");
    return 0;
}
