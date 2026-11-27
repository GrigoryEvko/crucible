// The loader refuses a malformed trace file with the error of its reason,
// and it does not crash.

#include <crucible/TraceLoader.h>
#include <crucible/SchemaTable.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Process.h>

#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/File.h>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The header of a trace file as the format lays it out: the magic, the
// version and the two counts, sixteen bytes with no padding.  A case writes
// the bytes of the class, so each field has the value of the case.
struct HeaderImage {
    ::fixy::FixedArray<std::byte, 4> magic{};
    uint32_t version = 0;
    uint32_t n_ops = 0;
    uint32_t n_metas = 0;
};

// The magic of a trace file.
constexpr ::fixy::FixedArray<std::byte, 4> kMagic{std::byte{'C'}, std::byte{'R'}, std::byte{'T'}, std::byte{'R'}};

// The bytes of a trace file that a case puts one after the other: a header,
// records and a name table.  The largest image of the cases, a name of
// SCHEMA_NAME_MAX characters, holds that name, its head and the header.
using TraceImage = ::fixy::FixedList<std::byte, crucible::SCHEMA_NAME_MAX + 8192>;

// The size of an empty name table: its count of zero names.
constexpr std::size_t kEmptyNameTable = sizeof(uint32_t);

// The roles of the two tensor counts of an op record.
struct InputCountRole final {};
struct OutputCountRole final {};
using InputCount = ::fixy::Side<InputCountRole, uint16_t>;
using OutputCount = ::fixy::Side<OutputCountRole, uint16_t>;

}  // namespace

using namespace crucible;
using crucible::detail::write_record_field_;

// True when the table gives the expected characters.
static bool is_named(::fixy::Option<InternedSchemaName> const& name, ::fixy::TextView expected) {
    return name.match([expected](InternedSchemaName const& found) noexcept { return found.value().view() == expected; },
                      [] noexcept { return false; });
}

static bool missing(::fixy::Option<InternedSchemaName> const& name) { return name.is_none(); }

// A load that registers schema names takes the context of a Vigil's producer
// claim.  These tests load without a Vigil, so they take that context from
// the test door.
constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

// Each load reads a file, so it also takes a context that owns IO and Block.
constexpr ::fixy::TestRunnerCtx kTestIo{::foundation::effects::testing::test()};

// A load that registers names takes the reserve of the program.  These tests
// reserve no hash, other than the cases of the reserve.
constexpr SchemaNameReserve kNoReservedNames = SchemaNameReserve::none();

// The sanitized form of the path of a test file.  No path of these tests
// holds a ".." component, so each one passes the check.
static ::fixy::Path<::fixy::tags::source::Sanitized> sanitized(::fixy::TextView raw) {
    return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(raw))
        .expect("each path of these tests passes the check");
}

// The working directory of the test.  It is in the build directory of the
// test, so two builds on one host write two files.
static ::fixy::fs::DirectoryHandle open_working_directory() {
    return ::fixy::fs::open_directory(kTestIo, sanitized(::fixy::TextView{"."})).expect("the working directory opens");
}

// Writes the bytes of image to the file file_name in the working directory of
// the test, and gives the path that a load takes.  The caller removes the
// file with remove_image.
static ::fixy::Path<::fixy::tags::source::Sanitized> write_image(::fixy::View<std::byte const> image,
                                                                 ::fixy::CText const& file_name) {
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    using TraceFile = ::fixy::fs::FileOf<WriteTruncate>;
    const ::fixy::fs::DirectoryHandle here = open_working_directory();
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(kTestIo, here.ref(), file_name)
                                .match(
                                    [image](::fixy::Linear<TraceFile> opened) noexcept {
                                        const TraceFile file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(kTestIo, file.ref(), image).is_ok();
                                    },
                                    [](::foundation::os::Errno) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    return sanitized(file_name.view());
}

// Removes the file file_name from the working directory of the test.
static void remove_image(::fixy::CText const& file_name) {
    const ::fixy::fs::DirectoryHandle here = open_working_directory();
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::unlink_at(kTestIo, here.ref(), file_name).is_ok());
}

// The load of an image of a trace file, written in the working directory of
// the test under the name file_name and removed after the load.
static ::fixy::Result<LoadedTrace, TraceLoadError> load_image(::fixy::View<std::byte const> image,
                                                              ::fixy::CText const& file_name) {
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(image, file_name);
    ::fixy::Result<LoadedTrace, TraceLoadError> loaded = load_trace(kTestIo, path);
    remove_image(file_name);
    return loaded;
}

// An image of Bytes bytes of zero, with the bytes of header at its start.
template <std::size_t Bytes>
static ::fixy::FixedArray<std::byte, Bytes> make_image(HeaderImage const& header) {
    return ::fixy::build_fixed_array<std::byte, Bytes>([&header](auto run) noexcept {
        ::fixy::copy(::fixy::take_window<sizeof(HeaderImage)>(std::move(run), 0).expect("the header fits the image"),
                     ::fixy::bytes(header));
    });
}

// True when the load refused its file for the reason error.
static bool is_refused_as(::fixy::Result<LoadedTrace, TraceLoadError> const& loaded, TraceLoadError error) {
    return loaded.err() == ::fixy::some(error);
}

static void test_missing_file() {
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized("/definitely/does/not/exist.crtrace")),
                                              TraceLoadError::OpenFailed));
    ::fixy::check::pass(kTest, "  test_missing_file:              PASSED\n");
}

static void test_empty_file() {
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_image(::fixy::View<std::byte const>{}, "crtrace-empty.bin"),
                                              TraceLoadError::HeaderTruncated));
    ::fixy::check::pass(kTest, "  test_empty_file:                PASSED\n");
}

static void test_bad_magic() {
    // Only the magic is wrong.  The counts look valid.
    const HeaderImage hdr{.magic = {std::byte{'X'}, std::byte{'X'}, std::byte{'X'}, std::byte{'X'}},
                          .version = crucible::TRACE_FORMAT_VERSION,
                          .n_ops = 0,
                          .n_metas = 0};
    CRUCIBLE_TEST_ASSERT(
        kTest, is_refused_as(load_image(::fixy::bytes(hdr), "crtrace-bad-magic.bin"), TraceLoadError::WrongMagic));
    ::fixy::check::pass(kTest, "  test_bad_magic:                 PASSED\n");
}

static void test_wrong_version() {
    const HeaderImage hdr{.magic = kMagic, .version = 99, .n_ops = 0, .n_metas = 0};
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_image(::fixy::bytes(hdr), "crtrace-wrong-version.bin"),
                                              TraceLoadError::WrongVersion));
    ::fixy::check::pass(kTest, "  test_wrong_version:             PASSED\n");
}

static void test_truncated_header() {
    // Eight bytes, so the two count fields are missing.
    const ::fixy::FixedArray<std::byte, 8> buf{std::byte{'C'}, std::byte{'R'}, std::byte{'T'}, std::byte{'R'},
                                               std::byte{1},   std::byte{0},   std::byte{0},   std::byte{0}};
    CRUCIBLE_TEST_ASSERT(
        kTest, is_refused_as(load_image(buf.view(), "crtrace-truncated-header.bin"), TraceLoadError::HeaderTruncated));
    ::fixy::check::pass(kTest, "  test_truncated_header:          PASSED\n");
}

static void test_truncated_op_records() {
    // The header claims five ops, and the file holds forty bytes after
    // it, which is less than one op record.
    const HeaderImage hdr{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 5, .n_metas = 0};
    const ::fixy::FixedArray<std::byte, 16 + 40> buf = make_image<16 + 40>(hdr);
    // The size check refuses the counts before the loader reads a record.
    CRUCIBLE_TEST_ASSERT(
        kTest, is_refused_as(load_image(buf.view(), "crtrace-truncated-ops.bin"), TraceLoadError::CountPastFile));
    ::fixy::check::pass(kTest, "  test_truncated_ops:             PASSED\n");
}

static void test_happy_path_zero_ops() {
    // The smallest valid file is the header with both counts at zero, and a
    // name table of no name.  The header alone ends before the count of the
    // table, so it is a cut file.
    const HeaderImage hdr{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0, .n_metas = 0};
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_image(::fixy::bytes(hdr), "crtrace-header-alone.bin"),
                                              TraceLoadError::NameTableTruncated));
    const ::fixy::FixedArray<std::byte, 16 + kEmptyNameTable> image = make_image<16 + kEmptyNameTable>(hdr);
    const LoadedTrace t =
        load_image(image.view(), "crtrace-zero-ops.bin").expect("a header with zero counts and no name loads");
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.size() == 0);
    ::fixy::check::pass(kTest, "  test_happy_path_empty:          PASSED\n");
}

static void test_adversarial_num_ops_rejected() {
    // A count this large must be rejected before the loader tries to
    // allocate hundreds of gigabytes of op records.
    const HeaderImage hdr{
        .magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0xFFFFFFFFu, .n_metas = 0};
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_image(::fixy::bytes(hdr), "crtrace-adversarial.bin"),
                                              TraceLoadError::CountPastLimit));
    ::fixy::check::pass(kTest, "  test_adversarial_counts:        PASSED\n");
}

// Puts value at the offset FieldOffset of the bytes.
template <std::size_t FieldOffset, class Field, std::size_t Count>
    requires(FieldOffset + sizeof(Field) <= Count)
static void put_field(::fixy::FixedArray<std::byte, Count>& bytes, Field value) {
    bytes.copy_window(FieldOffset, ::fixy::bytes(value)).expect("the bound of the template puts the field inside");
}

static void test_round_trip_single_op() {
    // Header, one op record, no metas, and a name table of no name.
    constexpr std::size_t kBytes = 16 + sizeof(TraceOpRecord) + kEmptyNameTable;
    ::fixy::FixedArray<std::byte, kBytes> buf = make_image<kBytes>(
        HeaderImage{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 1, .n_metas = 0});

    const uint64_t schema = 0xAABBCCDDEEFF0011ULL;
    const uint64_t shape = 0x1122334455667788ULL;
    const uint64_t scope = 0xDEADBEEFCAFEBABEULL;
    const uint64_t callsite = 0xFEEDFACE000000ADULL;
    put_field<16 + 0>(buf, schema);
    put_field<16 + 8>(buf, shape);
    put_field<16 + 16>(buf, scope);
    put_field<16 + 24>(buf, callsite);
    // The rest of the record is the scalar values, the tensor and scalar
    // counts, the flag bytes, the depth, the tags and the pad, all left zero.
    // The round trip of the encoders below gives the depth a value.

    const LoadedTrace t = load_image(buf.view(), "crtrace-single-op.bin").expect("a trace of one op loads");
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 1);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->schema_hash.raw() == schema);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->shape_hash.raw() == shape);
    CRUCIBLE_TEST_ASSERT(kTest, t.scope_hashes.get(0).expect("the trace holds one op")->raw() == scope);
    CRUCIBLE_TEST_ASSERT(kTest, t.callsite_hashes.get(0).expect("the trace holds one op")->raw() == callsite);
    CRUCIBLE_TEST_ASSERT(kTest, t.depths.get(0).expect("the trace holds one op")->is_top());
    ::fixy::check::pass(kTest, "  test_round_trip_single_op:      PASSED\n");
}

// The three encoders of the writer give a file that the loader reads back
// field by field.  The data pointer of a meta is an address of the process,
// and the encoder writes zero in its place.
static void test_encoders_round_trip_through_the_loader() {
    const ::fixy::FixedArray<TensorDim, 2> dims{tensor_dim(3), tensor_dim(4)};
    TensorMeta meta{};
    meta.set_shape(contiguous_tensor_shape(TensorSizes{dims.view()}).expect("a contiguous shape of rank 2"));
    meta.data_ptr = external_data_ptr(&meta);
    meta.dtype = ScalarType::Float;
    meta.requires_grad = true;
    meta.output_nr = 1;
    meta.storage_offset = 5;
    meta.version = 7;
    meta.storage_nbytes = 64;

    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0xAABBCCDDEEFF0011ULL};
    record.shape_hash = ShapeHash{0x1122334455667788ULL};
    record.scope_hash = ScopeHash{0xDEADBEEFCAFEBABEULL};
    record.callsite_hash = CallsiteHash{0xFEEDFACE000000ADULL};
    record.scalar_values.set(::fixy::index<0>(), -9);
    record.num_inputs = 1;
    record.num_scalars = 1;
    record.grad_enabled = 1;
    record.depth = OpDepth{5};
    // The one scalar is a double, and slot 4 has the tag NONE: the tag of
    // slot 4 goes into the high bits of op_flags.
    record.scalar_types = static_cast<uint16_t>(static_cast<uint16_t>(ScalarType2::FLOAT)
                                                | (static_cast<uint16_t>(ScalarType2::NONE) << 8));
    // The one scalar is a data scalar.
    record.data_slots = 1;

    const ::fixy::FixedArray<std::byte, TRACE_META_BYTES> meta_bytes = encode_trace_meta(meta);
    for (const std::byte byte : meta_bytes.view()
                                    .window<sizeof(uint64_t)>(offsetof(TensorMeta, data_ptr))
                                    .expect("the data pointer lies inside the record")) {
        CRUCIBLE_TEST_ASSERT(kTest, byte == std::byte{0});
    }

    // The header, one op, one meta, and a name table of no name.
    constexpr size_t kOpAt = TRACE_HEADER_BYTES;
    constexpr size_t kMetaAt = kOpAt + sizeof(TraceOpRecord);
    constexpr size_t kBytes = kMetaAt + TRACE_META_BYTES + sizeof(uint32_t);
    const ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header_bytes = encode_trace_header(1, 1);
    const ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> op_bytes = encode_trace_op(record);
    const ::fixy::FixedArray<std::byte, kBytes> image =
        ::fixy::build_fixed_array<std::byte, kBytes>([&header_bytes, &op_bytes, &meta_bytes](auto run) noexcept {
            run = ::fixy::reborrow(std::move(run), [&header_bytes](auto lent) noexcept {
                ::fixy::copy(
                    ::fixy::take_window<TRACE_HEADER_BYTES>(std::move(lent), 0).expect("the header fits the image"),
                    header_bytes.view());
            });
            run = ::fixy::reborrow(std::move(run), [&op_bytes](auto lent) noexcept {
                ::fixy::copy(
                    ::fixy::take_window<sizeof(TraceOpRecord)>(std::move(lent), kOpAt).expect("the op fits the image"),
                    op_bytes.view());
            });
            ::fixy::copy(
                ::fixy::take_window<TRACE_META_BYTES>(std::move(run), kMetaAt).expect("the meta fits the image"),
                meta_bytes.view());
        });

    const LoadedTrace trace = load_image(image.view(), "crtrace-encoders.bin").expect("an encoded trace loads");

    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 1);
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_metas == 1);
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.schema_hash == record.schema_hash);
    CRUCIBLE_TEST_ASSERT(kTest, entry.shape_hash == record.shape_hash);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_inputs == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_outputs == 0);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_scalar_args == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_values.at(::fixy::index<0>()) == -9);
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.is_grad_enabled());
    CRUCIBLE_TEST_ASSERT(kTest, *trace.scope_hashes.get(0).expect("the trace holds one op") == record.scope_hash);
    CRUCIBLE_TEST_ASSERT(kTest, *trace.callsite_hashes.get(0).expect("the trace holds one op") == record.callsite_hash);
    CRUCIBLE_TEST_ASSERT(kTest, *trace.depths.get(0).expect("the trace holds one op") == record.depth);
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == record.scalar_types);
    CRUCIBLE_TEST_ASSERT(kTest, entry.get_scalar_type(0) == ScalarType2::FLOAT
                                    && entry.get_scalar_type(4) == ScalarType2::NONE);
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.is_grad_enabled() && "the tag of slot 4 keeps the other op flags");
    CRUCIBLE_TEST_ASSERT(kTest, entry.data_slots()
                                            .expect("the loader keeps a word of the inline slots")
                                            .role(crucible::ScalarSlot{::fixy::index<0>()})
                                        == crucible::ScalarRole::DATA
                                    && "the loader keeps the role");

    const TensorMeta& loaded = *trace.metas.get(0).expect("the trace holds one meta");
    CRUCIBLE_TEST_ASSERT(kTest, loaded.ndim == 2);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.sizes[0].value() == 3 && loaded.sizes[1].value() == 4);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.strides[0].value() == 4 && loaded.strides[1].value() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.dtype == ScalarType::Float);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.requires_grad);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.output_nr == 1);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.storage_offset == 5);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.version == 7);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.storage_nbytes == 64);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.data_ptr.is_none());
    ::fixy::check::pass(kTest, "  test_encoders_round_trip:       PASSED\n");
}

// A trace of the first format (the first format of crucible/TraceLoader.h):
// an op record of 80 bytes with no depth, no tags and no data slots, and a
// metadata record of 168 bytes with a storage size of 32 bits at byte 156 and
// the hash of the gradient function at byte 160.  The image writes each field
// at the offset of that format, and the loader reads each field back.  The
// old loader refused the version: "the trace file has the version 1, and the
// loader reads 7".
static void test_first_format_loads() {
    constexpr size_t kOpAt = TRACE_HEADER_BYTES;
    constexpr size_t kMetaAt = kOpAt + TRACE_FIRST_OP_BYTES;
    constexpr size_t kBytes = kMetaAt + TRACE_FIRST_META_BYTES + sizeof(uint32_t);
    constexpr uint64_t kSchema = 0xAABBCCDDEEFF0011ULL;
    constexpr uint64_t kShape = 0x1122334455667788ULL;
    constexpr uint64_t kScope = 0xDEADBEEFCAFEBABEULL;
    constexpr uint64_t kCallsite = 0xFEEDFACE000000ADULL;
    constexpr uint64_t kGradFnHash = 0x0123456789ABCDEFULL;
    const ::fixy::FixedArray<std::byte, kBytes> image = ::fixy::build_fixed_array<std::byte, kBytes>(
        [kSchema, kShape, kScope, kCallsite, kGradFnHash](auto run) noexcept {
            run = ::fixy::reborrow(std::move(run), [](auto lent) noexcept {
                auto header =
                    ::fixy::take_window<TRACE_HEADER_BYTES>(std::move(lent), 0).expect("the header fits the image");
                write_record_field_<0>(header, TRACE_MAGIC);
                write_record_field_<4>(header, TRACE_FORMAT_FIRST_VERSION);
                write_record_field_<8>(header, uint32_t{1});
                write_record_field_<12>(header, uint32_t{1});
            });

            run = ::fixy::reborrow(std::move(run), [kSchema, kShape, kScope, kCallsite](auto lent) noexcept {
                auto op =
                    ::fixy::take_window<TRACE_FIRST_OP_BYTES>(std::move(lent), kOpAt).expect("the op fits the image");
                write_record_field_<0>(op, kSchema);
                write_record_field_<8>(op, kShape);
                write_record_field_<16>(op, kScope);
                write_record_field_<24>(op, kCallsite);
                write_record_field_<32>(op, int64_t{-9});
                write_record_field_<72>(op, uint16_t{0});
                write_record_field_<74>(op, uint16_t{1});
                write_record_field_<76>(op, uint16_t{1});
                write_record_field_<78>(op, uint8_t{1});
            });

            auto meta =
                ::fixy::take_window<TRACE_FIRST_META_BYTES>(std::move(run), kMetaAt).expect("the meta fits the image");
            write_record_field_<0>(meta, int64_t{3});
            write_record_field_<8>(meta, int64_t{4});
            write_record_field_<64>(meta, int64_t{4});
            write_record_field_<72>(meta, int64_t{1});
            write_record_field_<136>(meta, uint8_t{2});
            write_record_field_<137>(meta, std::to_underlying(ScalarType::Float));
            write_record_field_<138>(meta, std::to_underlying(DeviceType::CPU));
            write_record_field_<139>(meta, int8_t{-1});
            write_record_field_<140>(meta, std::to_underlying(Layout::Strided));
            write_record_field_<141>(meta, uint8_t{1});
            write_record_field_<143>(meta, uint8_t{1});
            write_record_field_<144>(meta, int64_t{5});
            write_record_field_<152>(meta, uint32_t{7});
            write_record_field_<156>(meta, uint32_t{64});
            write_record_field_<160>(meta, kGradFnHash);
        });

    const LoadedTrace trace =
        load_image(image.view(), ::fixy::CText{"crtrace-first-format.bin"}).expect("a trace of the first format loads");
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 1 && trace.num_metas == 1);
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.schema_hash.raw() == kSchema && entry.shape_hash.raw() == kShape);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_inputs == 0 && entry.num_outputs == 1 && entry.num_scalar_args == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_values.at(::fixy::index<0>()) == -9);
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.is_grad_enabled());
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == 0 && entry.get_scalar_type(0) == ScalarType2::INT);
    CRUCIBLE_TEST_ASSERT(kTest, entry.data_slots()
                                        .expect("the loader keeps a word of the inline slots")
                                        .role(crucible::ScalarSlot{::fixy::index<0>()})
                                    != crucible::ScalarRole::DATA);
    CRUCIBLE_TEST_ASSERT(kTest, trace.scope_hashes.get(0).expect("the trace holds one op")->raw() == kScope);
    CRUCIBLE_TEST_ASSERT(kTest, trace.callsite_hashes.get(0).expect("the trace holds one op")->raw() == kCallsite);
    CRUCIBLE_TEST_ASSERT(kTest, trace.depths.get(0).expect("the trace holds one op")->is_top());

    const TensorMeta& loaded = *trace.metas.get(0).expect("the trace holds one meta");
    CRUCIBLE_TEST_ASSERT(kTest, loaded.ndim == 2);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.sizes[0].value() == 3 && loaded.sizes[1].value() == 4);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.strides[0].value() == 4 && loaded.strides[1].value() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.dtype == ScalarType::Float && loaded.requires_grad && loaded.output_nr == 1);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.storage_offset == 5 && loaded.version == 7);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.storage_nbytes == 64);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.grad_fn_hash.value() == kGradFnHash);
    ::fixy::check::pass(kTest, "  test_first_format_loads:        PASSED\n");
}

// The recorded trace of the tree in the first format, the default trace of
// bench_phases, loads whole: 1091 ops and 2369 metadata records, each op at
// the depth 0.
static void test_recorded_first_format_trace_loads() {
    const LoadedTrace trace = load_trace(kTestIo, sanitized(CRUCIBLE_TEST_FIRST_FORMAT_TRACE))
                                  .expect("the recorded trace of the first format loads");
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 1091 && trace.num_metas == 2369);
    for (const OpDepth depth : trace.depths) {
        CRUCIBLE_TEST_ASSERT(kTest, depth.is_top());
    }
    ::fixy::check::pass(kTest, "  test_recorded_first_format:     PASSED\n");
}

// The load of a trace of one op record with no tensor and an empty name
// table, through the encoders.
static ::fixy::Result<LoadedTrace, TraceLoadError> load_one_op(TraceOpRecord const& record) {
    constexpr size_t kOpAt = TRACE_HEADER_BYTES;
    constexpr size_t kBytes = kOpAt + sizeof(TraceOpRecord) + sizeof(uint32_t);
    const ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header_bytes = encode_trace_header(1, 0);
    const ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> op_bytes = encode_trace_op(record);
    const ::fixy::FixedArray<std::byte, kBytes> image =
        ::fixy::build_fixed_array<std::byte, kBytes>([&header_bytes, &op_bytes](auto run) noexcept {
            run = ::fixy::reborrow(std::move(run), [&header_bytes](auto lent) noexcept {
                ::fixy::copy(
                    ::fixy::take_window<TRACE_HEADER_BYTES>(std::move(lent), 0).expect("the header fits the image"),
                    header_bytes.view());
            });
            ::fixy::copy(
                ::fixy::take_window<sizeof(TraceOpRecord)>(std::move(run), kOpAt).expect("the op fits the image"),
                op_bytes.view());
        });
    return load_image(image.view(), "crtrace-one-op.bin");
}

// The writers set only the four fields of the context in the flags byte of a
// record, a gradient byte of 0 or 1, and the tags of the five slots.  A record
// with a gradient byte of 2, the gradient mode or the tag of slot 4 in the
// flags byte, or a tag bit past the five slots is not a record that a writer
// produced, so the load refuses it.  The old loader took each of the four.
static void test_flags_and_tags_that_no_writer_gives_are_refused() {
    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0x5CA1A3ULL};
    record.inference_mode = 0x1F;
    record.grad_enabled = 1;
    record.scalar_types = 0x03FF;
    const LoadedTrace trace = load_one_op(record).expect("each field that a writer sets loads");
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.op_flags.bits() == 0xFF && entry.packed_scalar_types() == 0x03FF);

    TraceOpRecord gradient = record;
    gradient.grad_enabled = 2;
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_refused_as(load_one_op(gradient), TraceLoadError::OpCorrupt) && "a gradient byte of 2");
    TraceOpRecord stored_gradient = record;
    stored_gradient.inference_mode = 0x20;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(stored_gradient), TraceLoadError::OpCorrupt)
                                    && "the gradient mode in the flags byte");
    TraceOpRecord stored_tag = record;
    stored_tag.inference_mode = 0x40;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(stored_tag), TraceLoadError::OpCorrupt)
                                    && "the tag of slot 4 in the flags byte");
    TraceOpRecord tags = record;
    tags.scalar_types = 0x0400;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(tags), TraceLoadError::OpCorrupt)
                                    && "a tag bit past the five slots");
    ::fixy::check::pass(kTest, "  test_flags_and_tags_that_no_writer_gives_are_refused: PASSED\n");
}

// An op record with more scalars than the inline slots, or with a data slot
// past its count of scalars, is refused.  The build of a region ends the
// process on a count past the slots, and the replay binds a data slot from
// the stored values, so the load refuses each first.  The old loader took
// each count, so the first check loaded the op.
static void test_op_with_scalars_past_the_slots_is_refused() {
    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0x5CA1A2ULL};
    record.num_scalars = 6;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(record), TraceLoadError::OpCorrupt));

    record.num_scalars = 1;
    record.data_slots = 0b10;
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_refused_as(load_one_op(record), TraceLoadError::OpCorrupt) && "a data slot past the count");
    record.data_slots = 0b100000;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(record), TraceLoadError::OpCorrupt)
                                    && "a bit past the inline slots");

    record.data_slots = 0b1;
    const LoadedTrace trace = load_one_op(record).expect("a data slot below the count loads");
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_scalar_args == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.data_slots()
                                        .expect("the loader keeps a word of the inline slots")
                                        .role(crucible::ScalarSlot{::fixy::index<0>()})
                                    == crucible::ScalarRole::DATA);
    ::fixy::check::pass(kTest, "  test_op_scalars_past_the_slots: PASSED\n");
}

// The schema-name table ends the file, and each name carries a length field
// gated by a refinement over one to SCHEMA_NAME_LEN_MAX bytes.
//
// Each case clears the global table first, which also unseals it so a
// mutable view can be minted.
//
// The runtime guard and the refinement are a pair.  The guard means the
// refinement's constructor never sees an out-of-range value, and the
// refinement covers a reader that bypasses this loader.

// The scalar is appended in little-endian byte order: the low byte first.
template <::fixy::UnsignedInteger T>
static void append_le(TraceImage& buf, T v) {
    for (std::size_t shift = 0; shift < sizeof(T) * 8; shift += 8) {
        const bool is_put = buf.append(static_cast<std::byte>(static_cast<uint8_t>(v >> shift))).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_put);
    }
}

// Each character of text is appended as one byte, with no terminator.
static void append_text(TraceImage& buf, ::fixy::TextView text) {
    for (const char character : text) {
        const bool is_put = buf.append(static_cast<std::byte>(static_cast<unsigned char>(character))).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_put);
    }
}

// A sixteen-byte header with both counts at zero, ready for a name-table
// append.
static TraceImage make_zero_op_header() {
    TraceImage buf{};
    for (const std::byte byte : kMagic) {
        CRUCIBLE_TEST_ASSERT(kTest, buf.append(byte).is_ok());
    }
    append_le<uint32_t>(buf, crucible::TRACE_FORMAT_VERSION);  // version
    append_le<uint32_t>(buf, 0);  // n_ops
    append_le<uint32_t>(buf, 0);  // n_metas
    return buf;
}

// The trace of trace_path gives aten::add, aten::mul and aten::matmul to
// three hashes.  The table holds a different name of the hash of aten::mul
// first.  Input from outside can be wrong, so the table keeps its first name
// and refuses aten::mul.  The load refuses the whole trace, it registers no
// name of it, not even aten::add before aten::mul, it reports the reason, and
// the process lives.  The load runs in a child, so the test reads the report
// on the standard error of the child.  The old load registered aten::add and
// gave the trace.
static void
test_schema_name_table_collision_is_refused(::fixy::Path<::fixy::tags::source::Sanitized> const& trace_path) {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    constexpr SchemaHash kFirstHash{0xAAAA111122223333ULL};
    constexpr SchemaHash kHeldHash{0xBBBB444455556666ULL};
    constexpr SchemaHash kLaterHash{0xCCCC777788889999ULL};
    global_schema_table()
        .mint_mutable_view(kVigilForeground)
        .match(
            [&kHeldHash](SchemaTable::MutableView view) noexcept {
                const bool was_registered = register_schema_name(kTestIo, kTestIo.cap().alloc, view,
                                                                 InternalSchemaName{kHeldHash, "aten::held"})
                                                .is_ok();
                CRUCIBLE_TEST_ASSERT(kTest, was_registered);
            },
            []() noexcept {});

    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(
                kTestIo,
                [&trace_path] noexcept {
                    const bool is_refused =
                        is_refused_as(load_trace(kTestIo, kVigilForeground, trace_path, kNoReservedNames),
                                      TraceLoadError::NameCollision);
                    const bool skips_earlier = missing(global_schema_table().lookup(kFirstHash));
                    const bool keeps_held = is_named(global_schema_table().lookup(kHeldHash), "aten::held");
                    const bool skips_later = missing(global_schema_table().lookup(kLaterHash));
                    const bool keeps_count = global_schema_table().count() == 1;
                    if (!is_refused || !skips_earlier || !keeps_held || !skips_later || !keeps_count) {
                        ::fixy::fatal("the colliding name did not refuse the whole trace");
                    }
                })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::find(report.output(),
                            ::fixy::TextView{"load_trace: the schema table holds a different name of a schema hash of "
                                             "the trace, so the load refuses the trace and registers none of its 3 "
                                             "names"})
                   .is_some());
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(kHeldHash), "aten::held"));

    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_schema_name_table_collision_is_refused: PASSED\n");
}

// Registers count names from outside in the global table, each aten::filler
// on the hashes from 1.  A hash of the run time comes from outside, so each
// name goes through sanitize_schema_name.
static void register_outside_fillers(::fixy::ElementCount count) {
    global_schema_table()
        .mint_mutable_view(kVigilForeground)
        .match(
            [count](SchemaTable::MutableView view) noexcept {
                for (uint64_t filler = 1; filler <= count.raw(); ++filler) {
                    const SanitizedSchemaName name =
                        sanitize_schema_name(SchemaHash{filler}, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                                     ::fixy::TextView{"aten::filler"}))
                            .expect("each filler passes the validator");
                    const bool was_registered = register_schema_name(kTestIo, kTestIo.cap().alloc, view, name).is_ok();
                    CRUCIBLE_TEST_ASSERT(kTest, was_registered);
                }
            },
            []() noexcept {});
}

// The trace of trace_path gives aten::add, aten::mul and aten::matmul to
// three hashes, and the table holds two names less than the names from
// outside that it takes first.  The table has two places for names from
// outside, and the trace has three new names.  Input from outside can name
// more schemas than the table holds, so the load refuses the whole trace, it
// registers no name of it, it reports the reason, and the process lives.  The
// load runs in a child, so the test reads the report on the standard error of
// the child.  The old load put aten::add in the last place and gave the trace.
static void test_schema_name_table_full_is_refused(::fixy::Path<::fixy::tags::source::Sanitized> const& trace_path) {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    constexpr SchemaHash kFirstHash{0xAAAA111122223333ULL};
    constexpr SchemaHash kRefusedHash{0xBBBB444455556666ULL};
    constexpr SchemaHash kLaterHash{0xCCCC777788889999ULL};
    constexpr uint32_t kOutsideNames = SCHEMA_TABLE_CAP - SchemaTable::kInternalRoomNames;
    register_outside_fillers(::fixy::ElementCount{kOutsideNames - 2});
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == kOutsideNames - 2);

    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(::fixy::process::spawn_child(
                                  kTestIo,
                                  [&trace_path] noexcept {
                                      const bool is_refused = is_refused_as(
                                          load_trace(kTestIo, kVigilForeground, trace_path, kNoReservedNames),
                                          TraceLoadError::NameTableFull);
                                      const bool skips_first = missing(global_schema_table().lookup(kFirstHash));
                                      const bool skips_next = missing(global_schema_table().lookup(kRefusedHash));
                                      const bool skips_later = missing(global_schema_table().lookup(kLaterHash));
                                      const bool keeps_count = global_schema_table().count() == kOutsideNames - 2;
                                      if (!is_refused || !skips_first || !skips_next || !skips_later || !keeps_count) {
                                          ::fixy::fatal("the full schema table took a name of a refused trace");
                                      }
                                  })
                                  .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::find(report.output(),
                            ::fixy::TextView{"load_trace: the schema table takes at most 480 names from outside and "
                                             "has no place for each new name of the trace, so the load refuses the "
                                             "trace and registers none of its 3 names"})
                   .is_some());

    // A table with three places for names from outside takes the three new
    // names of the trace.  The room of internal names stays out of the count.
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    register_outside_fillers(::fixy::ElementCount{kOutsideNames - 3});
    CRUCIBLE_TEST_ASSERT(kTest, load_trace(kTestIo, kVigilForeground, trace_path, kNoReservedNames).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == kOutsideNames);
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(kLaterHash), "aten::matmul"));

    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_schema_name_table_full_is_refused: PASSED\n");
}

// The hash of aten::mul in the trace of the round trip.
constexpr SchemaHash kMulHash{0xBBBB444455556666ULL};

// A reserve that holds the hash of aten::mul for another name, and a reserve
// that holds it for aten::mul.
static constexpr ::fixy::Option<SchemaNameParts> find_other_name_on_mul_hash(SchemaHash hash) noexcept {
    if (hash != kMulHash) return ::fixy::Option<SchemaNameParts>{::fixy::none};
    return ::fixy::some(SchemaNameParts{::fixy::TextView{"aten::other"}});
}
static constexpr ::fixy::Option<SchemaNameParts> find_mul_name_on_mul_hash(SchemaHash hash) noexcept {
    if (hash != kMulHash) return ::fixy::Option<SchemaNameParts>{::fixy::none};
    return ::fixy::some(SchemaNameParts{::fixy::TextView{"aten::mul"}});
}

// The trace of trace_path gives aten::mul to the hash kMulHash.  A program
// that reserves the hash for another name has an internal name of the hash,
// and the internal registration would end the process at the collision.  So
// the load refuses the whole trace before it registers a name, and the
// process lives.  A reserve of the same name loads the trace.  The old load
// took no reserve, registered aten::mul, and the later internal name then
// ended the process.
static void
test_schema_name_reserved_for_other_name_is_refused(::fixy::Path<::fixy::tags::source::Sanitized> const& trace_path) {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    constexpr SchemaNameReserve kOtherName{SchemaNameSearchOf<&find_other_name_on_mul_hash>{}};
    constexpr SchemaNameReserve kSameName{SchemaNameSearchOf<&find_mul_name_on_mul_hash>{}};
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, kVigilForeground, trace_path, kOtherName),
                                              TraceLoadError::NameReserved));
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, load_trace(kTestIo, kVigilForeground, trace_path, kSameName).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(kMulHash), "aten::mul"));
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_schema_name_reserved_for_other_name_is_refused: PASSED\n");
}

static void test_schema_name_table_round_trip() {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 3);  // num_names

    auto append_entry = [&](SchemaHash hash, ::fixy::TextView name) {
        const uint16_t len = ::fixy::narrow<uint16_t>(name.size()).expect("a name of the test fits its length field");
        append_le<uint64_t>(buf, hash.raw());
        append_le<uint16_t>(buf, len);
        append_text(buf, name);
    };
    append_entry(SchemaHash{0xAAAA111122223333ULL}, "aten::add");
    append_entry(SchemaHash{0xBBBB444455556666ULL}, "aten::mul");
    append_entry(SchemaHash{0xCCCC777788889999ULL}, "aten::matmul");

    constexpr ::fixy::CText kFileName{"crtrace-names.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(buf.view(), kFileName);

    // A load with no producer context reads the names and registers none.
    CRUCIBLE_TEST_ASSERT(kTest, load_trace(kTestIo, path).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);

    const LoadedTrace t =
        load_trace(kTestIo, kVigilForeground, path, kNoReservedNames).expect("a trace with names loads");

    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 0);

    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xAAAA111122223333ULL}), "aten::add"));
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xBBBB444455556666ULL}), "aten::mul"));
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_named(global_schema_table().lookup(SchemaHash{0xCCCC777788889999ULL}), "aten::matmul"));

    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0xDEADBEEFULL})));

    test_schema_name_table_collision_is_refused(path);
    test_schema_name_table_full_is_refused(path);
    test_schema_name_reserved_for_other_name_is_refused(path);
    remove_image(kFileName);

    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_schema_name_table_round_trip: PASSED\n");
}

static void test_schema_name_table_corrupt_zero_len() {
    // A length of zero is the lower-bound corruption.  Without the
    // minimum, the hash would be bound to the empty string, and every later
    // lookup of that hash would return it.  The file is corrupt, so the load
    // refuses it whole.
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);  // num_names
    append_le<uint64_t>(buf, 0xDEADBEEFCAFEBABEULL);  // schema_hash
    append_le<uint16_t>(buf, 0);  // Corrupt name_len

    constexpr ::fixy::CText kFileName{"crtrace-zero-len.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(buf.view(), kFileName);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, kVigilForeground, path, kNoReservedNames),
                                              TraceLoadError::NameLengthPastLimit));
    remove_image(kFileName);

    // The check refused the image before the registration, so the hash
    // never reached the table.
    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0xDEADBEEFCAFEBABEULL})));
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);

    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_schema_name_table_corrupt_zero_len: PASSED\n");
}

// The longest name is the largest 16-bit length, so no length of a file is
// past the bound: the type of the field carries the upper bound, and the gate
// refuses only the length zero.  The fixture neg_schema_name_len_above_max
// keeps the refusal of a wider length.
static_assert(SCHEMA_NAME_LEN_MAX == UINT16_MAX);

static void test_schema_name_table_corrupt_oversize_len() {
    // The longest length of a name of a file, 65535, is the longest name that
    // the schema table holds, and the gate takes it.
    static_assert(kSchemaNameLenRange(uint16_t{UINT16_MAX}));
    ::fixy::check::pass(kTest, "  test_schema_name_table_corrupt_oversize_len: PASSED\n");
}

// True when the check of image refuses it for the reason error, and a load
// that registers the names refuses it for the same reason and registers no
// name.
static bool is_refused_whole(TraceImage const& image, TraceLoadError error) {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    constexpr ::fixy::CText kFileName{"crtrace-refused-name.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(image.view(), kFileName);
    const bool is_load_refused = is_refused_as(load_trace(kTestIo, kVigilForeground, path, kNoReservedNames), error);
    remove_image(kFileName);
    return CheckedTraceImage::check(image.view(), kTest.cap().alloc).err() == ::fixy::some(error) && is_load_refused
        && global_schema_table().count() == 0;
}

// A name that sanitize_schema_name refuses, the one validator of the C entry
// and of the loader, refuses the whole file: a name of the schema hash zero,
// and a name that holds a zero byte.  Two names of one hash refuse it too,
// also when the two names are equal.  A valid first name stays out of the
// table.  The old
// loader registered aten::add, and then the name of the hash zero.
static void test_names_that_the_validator_refuses_are_refused() {
    auto zero_hash = make_zero_op_header();
    append_le<uint32_t>(zero_hash, 2);
    append_le<uint64_t>(zero_hash, 0xAAAA111122223333ULL);
    append_le<uint16_t>(zero_hash, 9);
    append_text(zero_hash, "aten::add");
    append_le<uint64_t>(zero_hash, 0);
    append_le<uint16_t>(zero_hash, 3);
    append_text(zero_hash, "abc");
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_whole(zero_hash, TraceLoadError::NameCorrupt));

    auto zero_byte = make_zero_op_header();
    append_le<uint32_t>(zero_byte, 1);
    append_le<uint64_t>(zero_byte, 0x2E5ULL);
    append_le<uint16_t>(zero_byte, 3);
    append_text(zero_byte, "a");
    CRUCIBLE_TEST_ASSERT(kTest, zero_byte.append(std::byte{0}).is_ok());
    append_text(zero_byte, "b");
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_whole(zero_byte, TraceLoadError::NameCorrupt));

    auto repeated = make_zero_op_header();
    append_le<uint32_t>(repeated, 2);
    for (int copy = 0; copy < 2; ++copy) {
        append_le<uint64_t>(repeated, 0x3E9ULL);
        append_le<uint16_t>(repeated, 7);
        append_text(repeated, "aten::x");
    }
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_whole(repeated, TraceLoadError::NameRepeated));
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_names_the_validator_refuses: PASSED\n");
}

// Appends one name of one character to a name table.
static void append_short_name(TraceImage& image, uint64_t hash) {
    append_le<uint64_t>(image, hash);
    append_le<uint16_t>(image, 1);
    append_text(image, "n");
}

// The refusal of the check of image, or no value.
static ::fixy::Option<TraceLoadError> check_error(TraceImage const& image) {
    return CheckedTraceImage::check(image.view(), kTest.cap().alloc).err();
}

// The spread of the hashes of a table in no order: an odd constant, so the
// products of the first TRACE_NAMES_MAX counts are distinct and not zero.
constexpr uint64_t kSpread = 0x9E3779B97F4A7C15ULL;

// The bytes of a trace with no op and a name table of TRACE_NAMES_MAX names
// of one character.  The name at position index takes the hash
// hash_at(index).  The table holds more bytes than a TraceImage, so the image
// lives on the heap.
template <class HashAt>
static ::fixy::HeapArray<std::byte> make_widest_name_table(HashAt hash_at) {
    constexpr std::size_t kNameBytes = sizeof(uint64_t) + sizeof(uint16_t) + 1;
    constexpr std::size_t kImageBytes =
        TRACE_HEADER_BYTES + sizeof(uint32_t) + std::size_t{TRACE_NAMES_MAX} * kNameBytes;
    return ::fixy::mint_heap_array_written<std::byte>(
        kTest.cap().alloc, kImageBytes, [&hash_at](::fixy::WriteLease<std::byte>& lease) noexcept {
            const auto put_le = [&lease](auto value) noexcept {
                for (std::size_t shift = 0; shift < sizeof(value) * 8; shift += 8)
                    lease.put(static_cast<std::byte>(static_cast<uint8_t>(value >> shift)));
            };
            const auto header = encode_trace_header(0, 0);
            for (const std::byte header_byte : header.view())
                lease.put(header_byte);
            put_le(uint32_t{TRACE_NAMES_MAX});
            for (uint32_t index = 0; index < TRACE_NAMES_MAX; ++index) {
                put_le(hash_at(index));
                put_le(uint16_t{1});
                lease.put(std::byte{'n'});
            }
        });
}

// The check of a repeated hash reads the whole table, and the first refusal
// in the order of the file still decides the error.  A repeated hash before a
// corrupt name gives NameRepeated, and a corrupt name before a repeated hash
// gives NameCorrupt.  In a table of TRACE_NAMES_MAX names in no order, the
// last name repeats the hash of the first one, and the check finds it.
static void test_first_refusal_of_the_table_decides() {
    auto repeat_first = make_zero_op_header();
    append_le<uint32_t>(repeat_first, 3);
    append_short_name(repeat_first, 0x3E9ULL);
    append_short_name(repeat_first, 0x3E9ULL);
    append_short_name(repeat_first, 0);
    CRUCIBLE_TEST_ASSERT(kTest, check_error(repeat_first) == ::fixy::some(TraceLoadError::NameRepeated));

    auto corrupt_first = make_zero_op_header();
    append_le<uint32_t>(corrupt_first, 3);
    append_short_name(corrupt_first, 0x3E9ULL);
    append_short_name(corrupt_first, 0);
    append_short_name(corrupt_first, 0x3E9ULL);
    CRUCIBLE_TEST_ASSERT(kTest, check_error(corrupt_first) == ::fixy::some(TraceLoadError::NameCorrupt));

    const ::fixy::HeapArray<std::byte> repeat_last = make_widest_name_table([](uint32_t index) noexcept {
        return index + 1 == TRACE_NAMES_MAX ? kSpread : kSpread * (uint64_t{index} + 1);
    });
    CRUCIBLE_TEST_ASSERT(kTest, CheckedTraceImage::check(repeat_last.view(), kTest.cap().alloc).err()
                                    == ::fixy::some(TraceLoadError::NameRepeated));
    ::fixy::check::pass(kTest, "  test_first_refusal_decides:     PASSED\n");
}

// An image as the writer of the export writes it: two ops, with one input
// and one output each, four metadata records, and a name table of two
// names.  The sections end at the offsets of CutSections.
static TraceImage make_written_image() {
    TraceImage image{};
    const auto append_bytes = [&image](::fixy::View<std::byte const> bytes) {
        for (const std::byte byte : bytes) {
            CRUCIBLE_TEST_ASSERT(kTest, image.append(byte).is_ok());
        }
    };
    const ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header = encode_trace_header(2, 4);
    append_bytes(header.view());
    for (uint64_t op = 1; op <= 2; ++op) {
        TraceOpRecord record{};
        record.schema_hash = SchemaHash{op == 1 ? 0xAAAA111122223333ULL : 0xBBBB444455556666ULL};
        record.num_inputs = 1;
        record.num_outputs = 1;
        const ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> op_bytes = encode_trace_op(record);
        append_bytes(op_bytes.view());
    }
    const ::fixy::FixedArray<TensorDim, 1> dims{tensor_dim(4)};
    TensorMeta meta{};
    meta.set_shape(contiguous_tensor_shape(TensorSizes{dims.view()}).expect("a contiguous shape of rank 1"));
    meta.dtype = ScalarType::Float;
    const ::fixy::FixedArray<std::byte, TRACE_META_BYTES> meta_bytes = encode_trace_meta(meta);
    for (int record_index = 0; record_index < 4; ++record_index) {
        append_bytes(meta_bytes.view());
    }
    append_le<uint32_t>(image, 2);
    append_le<uint64_t>(image, 0xAAAA111122223333ULL);
    append_le<uint16_t>(image, 9);
    append_text(image, "aten::add");
    append_le<uint64_t>(image, 0xBBBB444455556666ULL);
    append_le<uint16_t>(image, 9);
    append_text(image, "aten::mul");
    return image;
}

// The offsets where the sections of make_written_image end: the header, the
// op records, the metadata records, the name count and the first name.
struct CutSections {
    std::size_t header = TRACE_HEADER_BYTES;
    std::size_t ops = TRACE_HEADER_BYTES + 2 * sizeof(TraceOpRecord);
    std::size_t metas = TRACE_HEADER_BYTES + 2 * sizeof(TraceOpRecord) + 4 * TRACE_META_BYTES;
    std::size_t count = TRACE_HEADER_BYTES + 2 * sizeof(TraceOpRecord) + 4 * TRACE_META_BYTES + sizeof(uint32_t);
    std::size_t first_name = TRACE_HEADER_BYTES + 2 * sizeof(TraceOpRecord) + 4 * TRACE_META_BYTES + sizeof(uint32_t)
                           + sizeof(uint64_t) + sizeof(uint16_t) + 9;
};

// The error of the check of the image cut to its first length bytes, or no
// value when the check accepts it.
static ::fixy::Option<TraceLoadError> cut_error(TraceImage const& image, std::size_t length) {
    const ::fixy::View<std::byte const> cut = image.view().window(0, length).expect("a cut is a prefix of the image");
    return CheckedTraceImage::check(cut, kTest.cap().alloc).err();
}

// Each cut of a file that the writer wrote is refused, at each byte, with
// the error of the section that the cut ends in.  The whole image passes.
// The old loader took a file that ends after the metadata as a trace with no
// name, and a file cut in the name table as a trace with the names before
// the cut.
static void test_each_cut_of_a_written_file_is_refused() {
    const TraceImage image = make_written_image();
    const CutSections sections{};
    CRUCIBLE_TEST_ASSERT(kTest, image.size() == sections.first_name + sizeof(uint64_t) + sizeof(uint16_t) + 9);
    CRUCIBLE_TEST_ASSERT(kTest, CheckedTraceImage::check(image.view(), kTest.cap().alloc).is_ok());
    for (std::size_t length = 0; length < image.size(); ++length) {
        TraceLoadError expected = TraceLoadError::NameTableTruncated;
        if (length < sections.header) {
            expected = TraceLoadError::HeaderTruncated;
        } else if (length < sections.metas) {
            expected = TraceLoadError::CountPastFile;
        }
        CRUCIBLE_TEST_ASSERT(kTest, cut_error(image, length) == ::fixy::some(expected));
    }
    ::fixy::check::pass(kTest, "  test_each_cut_is_refused:       PASSED\n");
}

// A load of a file cut at each section boundary is refused, and it registers
// no name of the file.  The cut after the first name is the case of the old
// loader that registered aten::add and gave a trace.
static void test_cut_files_at_the_section_boundaries_are_refused() {
    const TraceImage image = make_written_image();
    const CutSections sections{};
    const ::fixy::FixedArray<std::size_t, 5> boundaries{sections.header, sections.ops, sections.metas, sections.count,
                                                        sections.first_name};
    constexpr ::fixy::CText kFileName{"crtrace-cut.bin"};
    for (const std::size_t length : boundaries) {
        global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
        const ::fixy::Path<::fixy::tags::source::Sanitized> path =
            write_image(image.view().window(0, length).expect("a cut is a prefix of the image"), kFileName);
        const ::fixy::Result<LoadedTrace, TraceLoadError> loaded =
            load_trace(kTestIo, kVigilForeground, path, kNoReservedNames);
        remove_image(kFileName);
        CRUCIBLE_TEST_ASSERT(kTest, loaded.is_err());
        CRUCIBLE_TEST_ASSERT(kTest, loaded.err() == cut_error(image, length));
        CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);
    }
    // The whole file loads, with each of its names.
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(image.view(), kFileName);
    const LoadedTrace trace =
        load_trace(kTestIo, kVigilForeground, path, kNoReservedNames).expect("the written file loads");
    remove_image(kFileName);
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 2 && trace.num_metas == 4);
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xAAAA111122223333ULL}), "aten::add"));
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xBBBB444455556666ULL}), "aten::mul"));
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_cut_files_are_refused:     PASSED\n");
}

// A byte after the last name, and a name count past kTraceNumNamesBound, are
// refused.
static void test_trailing_byte_and_count_past_the_bound_are_refused() {
    TraceImage trailing = make_written_image();
    CRUCIBLE_TEST_ASSERT(kTest, trailing.append(std::byte{0}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, CheckedTraceImage::check(trailing.view(), kTest.cap().alloc).err()
                                    == ::fixy::some(TraceLoadError::TrailingBytes));
    CRUCIBLE_TEST_ASSERT(
        kTest, is_refused_as(load_image(trailing.view(), "crtrace-trailing.bin"), TraceLoadError::TrailingBytes));

    auto counted = make_zero_op_header();
    append_le<uint32_t>(counted, TRACE_NAMES_MAX + 1);
    CRUCIBLE_TEST_ASSERT(kTest, CheckedTraceImage::check(counted.view(), kTest.cap().alloc).err()
                                    == ::fixy::some(TraceLoadError::CountPastLimit));
    ::fixy::check::pass(kTest, "  test_trailing_and_count:        PASSED\n");
}

// The format holds what the writer can write: a name of SCHEMA_NAME_LEN_MAX
// characters, and a table of TRACE_NAMES_MAX names, the union of the two
// schema tables.  The old gates refused the first after 256 characters and
// refused a table of more than 1024 names, though the late table holds
// LATE_SCHEMA_TABLE_CAP names.
static void test_the_writer_maxima_pass_the_check() {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    auto longest = make_zero_op_header();
    append_le<uint32_t>(longest, 1);
    append_le<uint64_t>(longest, 0x10E6ULL);
    append_le<uint16_t>(longest, SCHEMA_NAME_LEN_MAX);
    for (std::size_t character = 0; character < SCHEMA_NAME_LEN_MAX; ++character) {
        CRUCIBLE_TEST_ASSERT(kTest, longest.append(std::byte{'n'}).is_ok());
    }
    const LoadedTrace trace =
        load_image(longest.view(), "crtrace-longest-name.bin").expect("a name of the longest length loads");
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 0);
    // The load that registers the names puts the whole name in the table.
    // The old loader refused a name past 256 characters at the C door only.
    ::fixy::FixedText<SCHEMA_NAME_MAX> longest_name{};
    for (std::size_t character = 0; character < SCHEMA_NAME_MAX; ++character) {
        CRUCIBLE_TEST_ASSERT(kTest, longest_name.append('n').is_ok());
    }
    constexpr ::fixy::CText kLongestFile{"crtrace-longest-name-registered.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> longest_path = write_image(longest.view(), kLongestFile);
    const bool is_registered = load_trace(kTestIo, kVigilForeground, longest_path, kNoReservedNames).is_ok();
    remove_image(kLongestFile);
    CRUCIBLE_TEST_ASSERT(kTest, is_registered);
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0x10E6ULL}), longest_name.view()));
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});

    const ::fixy::HeapArray<std::byte> widest =
        make_widest_name_table([](uint32_t index) noexcept { return uint64_t{index} + 1; });
    const auto checked = CheckedTraceImage::check(widest.view(), kTest.cap().alloc);
    const bool holds_each_name =
        checked.match([](CheckedTraceImage const& image) noexcept { return image.num_names() == TRACE_NAMES_MAX; },
                      [](TraceLoadError) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each_name);
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_the_writer_maxima:         PASSED\n");
}

// The loader builds each op's meta start from a running cursor over its
// tensor counts, and every consumer indexes the metas from that start.
// A cursor that runs past the meta count turns those reads into an
// out-of-bounds read driven by an untrusted file.  The loader must reject
// such a file, so that the trace it returns is always self-consistent.

// The schema hash of the op of a case that tests another field.  A schema
// hash of zero breaks a claim of an entry, so the load refuses it.
constexpr uint64_t kCaseSchemaHash = 0xC0DE0001ULL;

// The load of a 1-op .crtrace with MetaCount metas, the given op tensor
// counts and a name table of no name.  The metas region is zero-filled
// (ndim=0, valid).  The file goes after the load.
template <uint32_t MetaCount>
static ::fixy::Result<LoadedTrace, TraceLoadError> load_one_op_with_metas(InputCount const& num_inputs,
                                                                          OutputCount const& num_outputs) {
    constexpr std::size_t kBytes =
        16 + sizeof(TraceOpRecord) + std::size_t{MetaCount} * TRACE_META_BYTES + kEmptyNameTable;
    ::fixy::FixedArray<std::byte, kBytes> buf = make_image<kBytes>(
        HeaderImage{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 1, .n_metas = MetaCount});
    // The op record begins at offset 16.  The schema hash is its first
    // field, and the two tensor counts sit at +72 and +74 inside it.
    put_field<16>(buf, kCaseSchemaHash);
    put_field<16 + 72>(buf, num_inputs.value());
    put_field<16 + 74>(buf, num_outputs.value());
    return load_image(buf.view(), "crtrace-one-op-metas.bin");
}

static void test_meta_overrun_rejected() {
    // Op claims 5 tensors (3 in + 2 out) but only 2 metas exist — the
    // running meta cursor would overrun the metas vector.  Reject.
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op_with_metas</*n_metas=*/2>(InputCount{3}, OutputCount{2}),
                                              TraceLoadError::MetaRunPastCount));
    ::fixy::check::pass(kTest, "  test_meta_overrun_rejected:     PASSED\n");
}

static void test_meta_exact_count_loads() {
    // Op claims exactly 5 tensors and 5 metas exist — in bounds, loads.
    const LoadedTrace t = load_one_op_with_metas</*n_metas=*/5>(InputCount{3}, OutputCount{2})
                              .expect("an op whose metadata fits the count loads");
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 1);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 5);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->num_inputs == 3);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->num_outputs == 2);
    CRUCIBLE_TEST_ASSERT(kTest, t.meta_starts.get(0).expect("the trace holds one op")->raw()
                                    == 0);  // sole op starts at metas[0]
    ::fixy::check::pass(kTest, "  test_meta_exact_count_loads:    PASSED\n");
}

static void test_meta_count_uint16_overflow_rejected() {
    // The two counts sum to 65536, which wraps to zero in 16 bits.  The
    // validator of an entry refuses the 65535 inputs before the sum.
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op_with_metas</*n_metas=*/2>(InputCount{65535}, OutputCount{1}),
                                              TraceLoadError::OpCorrupt));
    ::fixy::check::pass(kTest, "  test_meta_uint16_overflow:      PASSED\n");
}

// An op record that the trust ladder of the vessel refuses, the load refuses
// too, because the two doors call entry_is_well_formed.  The old loader took
// an op of the schema hash zero, and an op of 65 inputs or 65 outputs.
static void test_op_that_the_ladder_refuses_is_refused() {
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op(TraceOpRecord{}), TraceLoadError::OpCorrupt)
                                    && "the schema hash zero");
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op_with_metas<kMaxOpInputs + 1>(
                                                  InputCount{uint16_t{kMaxOpInputs + 1}}, OutputCount{0}),
                                              TraceLoadError::OpCorrupt)
                                    && "one input past the bound");
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_one_op_with_metas<kMaxOpOutputs + 1>(
                                                  InputCount{0}, OutputCount{uint16_t{kMaxOpOutputs + 1}}),
                                              TraceLoadError::OpCorrupt)
                                    && "one output past the bound");
    const LoadedTrace inputs = load_one_op_with_metas<kMaxOpInputs>(InputCount{kMaxOpInputs}, OutputCount{0})
                                   .expect("an op of the most inputs loads");
    CRUCIBLE_TEST_ASSERT(kTest, inputs.entries.get(0).expect("the trace holds one op")->num_inputs == kMaxOpInputs);
    const LoadedTrace outputs = load_one_op_with_metas<kMaxOpOutputs>(InputCount{0}, OutputCount{kMaxOpOutputs})
                                    .expect("an op of the most outputs loads");
    CRUCIBLE_TEST_ASSERT(kTest, outputs.entries.get(0).expect("the trace holds one op")->num_outputs == kMaxOpOutputs);
    ::fixy::check::pass(kTest, "  test_op_the_ladder_refuses:     PASSED\n");
}

// The peak resident size of this process, in kilobytes: the field VmHWM of
// its status file, such as 4096 in "VmHWM:\t    4096 kB".  It only grows,
// so the case that reads it runs first.  A status file that does not read,
// or a field that does not parse, ends the process, so no case compares a
// value that the read did not give.
static uint64_t peak_resident_kb() {
    ::fixy::FixedText<8192> status{};
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::os::read_file("/proc/self/status", status).is_ok());
    constexpr ::fixy::TextView kField{"VmHWM:"};
    const std::size_t value_at =
        ::fixy::find(status.view(), kField).expect("the status file holds the field VmHWM") + kField.size();
    ::fixy::TextView value =
        status.view().window(value_at, status.size() - value_at).expect("the field lies inside the status file");
    while (::fixy::starts_with(value, ::fixy::TextView{" "}) || ::fixy::starts_with(value, ::fixy::TextView{"\t"})) {
        value = value.window(1, value.size() - 1).expect("a blank lies inside the field");
    }
    const std::size_t digits = ::fixy::find(value, ' ').expect("the unit kB follows the count of the field VmHWM");
    return ::fixy::parse<uint64_t>(value.window(0, digits).expect("the digits lie inside the field"))
        .expect("the field VmHWM holds a decimal count");
}

static void test_counts_past_the_file_allocate_nothing() {
    // Each count is under its cap, and the file holds the header alone.
    // A loader that sizes its vectors from the counts before it compares
    // them with the file touches hundreds of megabytes here.
    const uint64_t peak_before_kb = peak_resident_kb();
    const ::fixy::FixedArray<HeaderImage, 2> claims{
        HeaderImage{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = MAX_OPS, .n_metas = 0u},
        HeaderImage{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0u, .n_metas = 1u << 20}};
    for (const HeaderImage& header : claims) {
        CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_image(::fixy::bytes(header), "crtrace-counts-past-the-file.bin"),
                                                  TraceLoadError::CountPastFile));
    }
    const uint64_t growth_kb = peak_resident_kb() - peak_before_kb;
    CRUCIBLE_TEST_ASSERT(kTest, growth_kb < 64 * 1024);
    ::fixy::check::pass(kTest, "  test_counts_past_the_file:      PASSED\n");
}

// One metadata record as the file carries it.  A record of zeros is a
// valid descriptor of rank zero.
using MetaRecord = ::fixy::FixedArray<std::byte, sizeof(TensorMeta)>;

// Loads a trace of one op with one tensor, whose metadata is the record, and a
// name table of no name.
static bool loads_with_meta(const MetaRecord& record) {
    constexpr std::size_t kMetaAt = 16 + sizeof(TraceOpRecord);
    constexpr std::size_t kBytes = kMetaAt + sizeof(TensorMeta) + kEmptyNameTable;
    ::fixy::FixedArray<std::byte, kBytes> buf = make_image<kBytes>(
        HeaderImage{.magic = kMagic, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 1, .n_metas = 1});
    put_field<16>(buf, kCaseSchemaHash);
    put_field<16 + 72>(buf, uint16_t{1});
    buf.copy_window(kMetaAt, record.view()).expect("the metadata record fits the image");
    const auto loaded = load_image(buf.view(), "crtrace-meta.bin");
    // Only the metadata record differs from a valid trace, so a refusal has
    // the one reason of a corrupt record.
    CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok() || is_refused_as(loaded, TraceLoadError::MetaCorrupt));
    return loaded.is_ok();
}

template <std::size_t FieldOffset, class Field>
static bool loads_with_meta_field(Field value) {
    MetaRecord record{};
    put_field<FieldOffset>(record, value);
    return loads_with_meta(record);
}

static void test_meta_bytes_break_no_claim_of_the_type() {
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta(MetaRecord{}));

    // A bool that holds any byte but zero or one is undefined to read.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field<offsetof(TensorMeta, requires_grad)>(uint8_t{2}));
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta_field<offsetof(TensorMeta, requires_grad)>(uint8_t{1}));

    // A size lane inside the rank must fit the extent bound.
    {
        MetaRecord rank_one{};
        put_field<offsetof(TensorMeta, ndim)>(rank_one, uint8_t{1});
        put_field<offsetof(TensorMeta, sizes)>(rank_one, kMaxTensorDimExtent);
        CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta(rank_one));
        put_field<offsetof(TensorMeta, sizes)>(rank_one, ::fixy::max_of<int64_t>);
        CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta(rank_one));
    }

    // A lane past the rank holds zero, so equal descriptors write equal
    // bytes.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field<offsetof(TensorMeta, sizes) + sizeof(int64_t) * 3>(int64_t{5}));
    CRUCIBLE_TEST_ASSERT(kTest,
                         !loads_with_meta_field<offsetof(TensorMeta, strides) + sizeof(int64_t) * 7>(int64_t{1}));

    // A flag bit that no meta_flags constant names is reserved.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field<offsetof(TensorMeta, flags)>(uint8_t{0x40}));
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta_field<offsetof(TensorMeta, flags)>(meta_flags::IS_CONJ));

    // A device index below the host is not a device.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field<offsetof(TensorMeta, device_idx)>(int8_t{-2}));

    // A rank past the bound is refused before a lane is read.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field<offsetof(TensorMeta, ndim)>(uint8_t{9}));
    ::fixy::check::pass(kTest, "  test_meta_bytes_checked:        PASSED\n");
}

// A sealed table takes no name, so a load that registers the names refuses a
// trace with names, and the table stays empty.  The old load gave the trace
// with no name in the table.  A trace with no name loads, and a load that
// skips the names loads each trace.
static void test_names_on_a_sealed_table_refuse_the_load() {
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    global_schema_table().seal(kTest);

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);  // num_names
    append_le<uint64_t>(buf, 0x5EA1ED0000000001ULL);
    constexpr ::fixy::TextView name{"aten::sealed"};
    append_le<uint16_t>(buf, ::fixy::narrow<uint16_t>(name.size()).expect("a name of the test fits its length field"));
    append_text(buf, name);

    constexpr ::fixy::CText kFileName{"crtrace-sealed.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> path = write_image(buf.view(), kFileName);
    const bool is_refused =
        is_refused_as(load_trace(kTestIo, kVigilForeground, path, kNoReservedNames), TraceLoadError::NameTableSealed);
    const bool is_skipped = load_trace(kTestIo, path).is_ok();
    remove_image(kFileName);

    CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    CRUCIBLE_TEST_ASSERT(kTest, is_skipped);
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().is_sealed());
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0x5EA1ED0000000001ULL})));

    auto nameless = make_zero_op_header();
    append_le<uint32_t>(nameless, 0);
    constexpr ::fixy::CText kNamelessFile{"crtrace-sealed-nameless.bin"};
    const ::fixy::Path<::fixy::tags::source::Sanitized> nameless_path = write_image(nameless.view(), kNamelessFile);
    const bool is_nameless_loaded = load_trace(kTestIo, kVigilForeground, nameless_path, kNoReservedNames).is_ok();
    remove_image(kNamelessFile);
    CRUCIBLE_TEST_ASSERT(kTest, is_nameless_loaded);

    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    ::fixy::check::pass(kTest, "  test_names_on_sealed_table:     PASSED\n");
}

int main() {
    test_counts_past_the_file_allocate_nothing();
    test_missing_file();
    test_empty_file();
    test_bad_magic();
    test_wrong_version();
    test_truncated_header();
    test_truncated_op_records();
    test_happy_path_zero_ops();
    test_adversarial_num_ops_rejected();
    test_round_trip_single_op();
    test_encoders_round_trip_through_the_loader();
    test_first_format_loads();
    test_recorded_first_format_trace_loads();
    test_op_with_scalars_past_the_slots_is_refused();
    test_flags_and_tags_that_no_writer_gives_are_refused();
    test_schema_name_table_round_trip();
    test_schema_name_table_corrupt_zero_len();
    test_schema_name_table_corrupt_oversize_len();
    test_names_that_the_validator_refuses_are_refused();
    test_first_refusal_of_the_table_decides();
    test_each_cut_of_a_written_file_is_refused();
    test_cut_files_at_the_section_boundaries_are_refused();
    test_trailing_byte_and_count_past_the_bound_are_refused();
    test_the_writer_maxima_pass_the_check();
    test_meta_overrun_rejected();
    test_meta_exact_count_loads();
    test_meta_count_uint16_overflow_rejected();
    test_op_that_the_ladder_refuses_is_refused();
    test_meta_bytes_break_no_claim_of_the_type();
    test_names_on_a_sealed_table_refuse_the_load();
    ::fixy::check::pass(kTest, "test_trace_loader: 27 groups, all passed\n");
    return 0;
}
