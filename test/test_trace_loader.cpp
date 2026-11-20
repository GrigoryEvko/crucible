// The loader refuses a malformed trace file with the error of its reason,
// and it does not crash.

#include <crucible/TraceLoader.h>
#include <crucible/SchemaTable.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Process.h>

#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

// True when the table gives the expected characters.
static bool is_named(::fixy::Option<SchemaTable::LookupName> const& name, ::fixy::TextView expected) {
    return name.match(
        [expected](SchemaTable::LookupName const& found) noexcept { return found.value().view() == expected; },
        [] noexcept { return false; });
}

static bool missing(::fixy::Option<SchemaTable::LookupName> const& name) { return name.is_none(); }

// A load that registers schema names takes the context of a Vigil's producer
// claim.  These tests load without a Vigil, so they take that context from
// the test door.
constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

// Each load reads a file, so it also takes a context that owns IO and Block.
constexpr ::fixy::TestRunnerCtx kTestIo{::foundation::effects::testing::test()};

// The caller removes the file when done.  The path is built from the
// process id and a counter because tmpnam is deprecated and its
// replacement signature warns on an unused result.
static std::string write_tmp(const void* data, size_t n) {
    static int ctr = 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/tmp/crtrace-%d-%d.bin", ::getpid(), ++ctr);
    std::FILE* f = std::fopen(buf, "wb");
    CRUCIBLE_TEST_ASSERT(kTest, f);
    std::fwrite(data, 1, n, f);
    std::fclose(f);
    return buf;
}

// The sanitized form of the path of a test file.  No path of these tests
// holds a ".." component, so each one passes the check.
static ::fixy::Path<::fixy::tags::source::Sanitized> sanitized(const std::string& raw) {
    return ::fixy::sanitize_path(
               ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::mint_foreign_text(raw.data(), raw.size())))
        .expect("each path of these tests passes the check");
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
    std::string path = write_tmp("", 0);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::HeaderTruncated));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_empty_file:                PASSED\n");
}

static void test_bad_magic() {
    // Only the magic is wrong.  The counts look valid.
    struct {
        char magic[4];
        uint32_t version;
        uint32_t n_ops;
        uint32_t n_metas;
    } hdr{.magic = {'X', 'X', 'X', 'X'}, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::WrongMagic));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_bad_magic:                 PASSED\n");
}

static void test_wrong_version() {
    struct {
        char magic[4];
        uint32_t version;
        uint32_t n_ops;
        uint32_t n_metas;
    } hdr{.magic = {'C', 'R', 'T', 'R'}, .version = 99, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::WrongVersion));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_wrong_version:             PASSED\n");
}

static void test_truncated_header() {
    // Eight bytes, so the two count fields are missing.
    char buf[8] = {'C', 'R', 'T', 'R', 1, 0, 0, 0};
    std::string path = write_tmp(buf, sizeof(buf));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::HeaderTruncated));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_truncated_header:          PASSED\n");
}

static void test_truncated_op_records() {
    // The header claims five ops, and the file holds forty bytes after
    // it, which is less than one op record.
    struct {
        char magic[4];
        uint32_t version;
        uint32_t n_ops;
        uint32_t n_metas;
    } hdr{.magic = {'C', 'R', 'T', 'R'}, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 5, .n_metas = 0};
    char buf[16 + 40] = {};
    std::memcpy(buf, &hdr, sizeof(hdr));
    std::string path = write_tmp(buf, sizeof(buf));
    // The size check refuses the counts before the loader reads a record.
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::CountPastFile));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_truncated_ops:             PASSED\n");
}

static void test_happy_path_zero_ops() {
    // The smallest valid file is the header alone, with both counts at
    // zero.
    struct {
        char magic[4];
        uint32_t version;
        uint32_t n_ops;
        uint32_t n_metas;
    } hdr{.magic = {'C', 'R', 'T', 'R'}, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    const LoadedTrace t = load_trace(kTestIo, sanitized(path)).expect("a header with zero counts loads");
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.size() == 0);
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_happy_path_empty:          PASSED\n");
}

static void test_adversarial_num_ops_rejected() {
    // A count this large must be rejected before the loader tries to
    // allocate hundreds of gigabytes of op records.
    struct {
        char magic[4];
        uint32_t version;
        uint32_t n_ops;
        uint32_t n_metas;
    } hdr{.magic = {'C', 'R', 'T', 'R'}, .version = crucible::TRACE_FORMAT_VERSION, .n_ops = 0xFFFFFFFFu, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::CountPastLimit));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_adversarial_counts:        PASSED\n");
}

static void test_round_trip_single_op() {
    // Header, one op record, no metas.
    char buf[16 + sizeof(TraceOpRecord)] = {};
    const char magic[4] = {'C', 'R', 'T', 'R'};
    const uint32_t version = crucible::TRACE_FORMAT_VERSION, n_ops = 1, n_metas = 0;
    std::memcpy(buf + 0, magic, 4);
    std::memcpy(buf + 4, &version, 4);
    std::memcpy(buf + 8, &n_ops, 4);
    std::memcpy(buf + 12, &n_metas, 4);

    const uint64_t schema = 0xAABBCCDDEEFF0011ULL;
    const uint64_t shape = 0x1122334455667788ULL;
    const uint64_t scope = 0xDEADBEEFCAFEBABEULL;
    const uint64_t callsite = 0xFEEDFACE000000ADULL;
    std::memcpy(buf + 16 + 0, &schema, 8);
    std::memcpy(buf + 16 + 8, &shape, 8);
    std::memcpy(buf + 16 + 16, &scope, 8);
    std::memcpy(buf + 16 + 24, &callsite, 8);
    // The rest of the record is the scalar values, the tensor and scalar
    // counts, the flag bytes, the depth, the tags and the pad, all left zero.
    // The round trip of the encoders below gives the depth a value.

    std::string path = write_tmp(buf, sizeof(buf));
    const LoadedTrace t = load_trace(kTestIo, sanitized(path)).expect("a trace of one op loads");
    std::remove(path.c_str());
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
    TensorMeta meta{};
    meta.ndim = 2;
    meta.sizes[0] = tensor_dim(3);
    meta.sizes[1] = tensor_dim(4);
    meta.strides[0] = tensor_dim(4);
    meta.strides[1] = tensor_dim(1);
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
    record.scalar_values.at(::fixy::index<0>()) = -9;
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
    ::fixy::FixedArray<std::byte, kBytes> image{};
    ::fixy::copy(image.view().window<TRACE_HEADER_BYTES>(0).expect("the header fits the image"), header_bytes.view());
    ::fixy::copy(image.view().window<sizeof(TraceOpRecord)>(kOpAt).expect("the op fits the image"), op_bytes.view());
    ::fixy::copy(image.view().window<TRACE_META_BYTES>(kMetaAt).expect("the meta fits the image"), meta_bytes.view());

    // The file is in the working directory of the test, which is in its build
    // directory, so two builds on one host write two files.
    constexpr ::fixy::CText kFileName{"crtrace-encoders.bin"};
    const auto checked = [](::fixy::TextView text) noexcept {
        return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(text))
            .expect("a plain relative path passes the check");
    };
    const ::fixy::fs::DirectoryHandle here =
        ::fixy::fs::open_directory(kTestIo, checked(::fixy::TextView{"."})).expect("the working directory opens");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    using TraceFile = ::fixy::fs::FileOf<WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(kTestIo, here.ref(), kFileName)
                                .match(
                                    [&image](::fixy::Linear<TraceFile> opened) noexcept {
                                        const TraceFile file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(kTestIo, file.ref(), image.view()).is_ok();
                                    },
                                    [](::foundation::os::Errno) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    const LoadedTrace trace = load_trace(kTestIo, checked(kFileName.view())).expect("an encoded trace loads");
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::unlink_at(kTestIo, here.ref(), kFileName).is_ok());

    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 1);
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_metas == 1);
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.schema_hash == record.schema_hash);
    CRUCIBLE_TEST_ASSERT(kTest, entry.shape_hash == record.shape_hash);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_inputs == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_outputs == 0);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_scalar_args == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_values.at(::fixy::index<0>()) == -9);
    CRUCIBLE_TEST_ASSERT(kTest, (entry.op_flags & op_flag::GRAD_ENABLED) != 0);
    CRUCIBLE_TEST_ASSERT(kTest, *trace.scope_hashes.get(0).expect("the trace holds one op") == record.scope_hash);
    CRUCIBLE_TEST_ASSERT(kTest, *trace.callsite_hashes.get(0).expect("the trace holds one op") == record.callsite_hash);
    CRUCIBLE_TEST_ASSERT(kTest, *trace.depths.get(0).expect("the trace holds one op") == record.depth);
    CRUCIBLE_TEST_ASSERT(kTest, entry.packed_scalar_types() == record.scalar_types);
    CRUCIBLE_TEST_ASSERT(kTest, entry.get_scalar_type(0) == ScalarType2::FLOAT
                                    && entry.get_scalar_type(4) == ScalarType2::NONE);
    CRUCIBLE_TEST_ASSERT(kTest,
                         (entry.op_flags & op_flag::GRAD_ENABLED) != 0 && "the tag of slot 4 keeps the other op flags");
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
    CRUCIBLE_TEST_ASSERT(kTest, loaded.data_ptr.value() == nullptr);
    ::fixy::check::pass(kTest, "  test_encoders_round_trip:       PASSED\n");
}

// The load of an image of a trace file, written in the working directory of
// the test under the name kFileName and removed after the load.
template <std::size_t Bytes>
static ::fixy::Result<LoadedTrace, TraceLoadError> load_image(::fixy::FixedArray<std::byte, Bytes> const& image,
                                                              ::fixy::CText const& file_name) {
    const auto checked = [](::fixy::TextView text) noexcept {
        return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(text))
            .expect("a plain relative path passes the check");
    };
    const ::fixy::fs::DirectoryHandle here =
        ::fixy::fs::open_directory(kTestIo, checked(::fixy::TextView{"."})).expect("the working directory opens");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    using TraceFile = ::fixy::fs::FileOf<WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(kTestIo, here.ref(), file_name)
                                .match(
                                    [&image](::fixy::Linear<TraceFile> opened) noexcept {
                                        const TraceFile file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(kTestIo, file.ref(), image.view()).is_ok();
                                    },
                                    [](::foundation::os::Errno) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    ::fixy::Result<LoadedTrace, TraceLoadError> loaded = load_trace(kTestIo, checked(file_name.view()));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::unlink_at(kTestIo, here.ref(), file_name).is_ok());
    return loaded;
}

// A trace of the first format (THE FIRST FORMAT of crucible/TraceLoader.h):
// an op record of 80 bytes with no depth, no tags and no data slots, and a
// metadata record of 168 bytes with a storage size of 32 bits at byte 156 and
// the hash of the gradient function at byte 160.  The image writes each field
// at the offset of that format, and the loader reads each field back.  The
// old loader refused the version: "the trace file has the version 1, and the
// loader reads 7".
static void test_first_format_loads() {
    using crucible::detail::write_record_field_;
    constexpr size_t kOpAt = TRACE_HEADER_BYTES;
    constexpr size_t kMetaAt = kOpAt + TRACE_FIRST_OP_BYTES;
    constexpr size_t kBytes = kMetaAt + TRACE_FIRST_META_BYTES + sizeof(uint32_t);
    constexpr uint64_t kSchema = 0xAABBCCDDEEFF0011ULL;
    constexpr uint64_t kShape = 0x1122334455667788ULL;
    constexpr uint64_t kScope = 0xDEADBEEFCAFEBABEULL;
    constexpr uint64_t kCallsite = 0xFEEDFACE000000ADULL;
    constexpr uint64_t kGradFnHash = 0x0123456789ABCDEFULL;
    ::fixy::FixedArray<std::byte, kBytes> image{};

    const ::fixy::View<std::byte, TRACE_HEADER_BYTES> header =
        image.view().window<TRACE_HEADER_BYTES>(0).expect("the header fits the image");
    write_record_field_<0>(header, TRACE_MAGIC);
    write_record_field_<4>(header, TRACE_FORMAT_FIRST_VERSION);
    write_record_field_<8>(header, uint32_t{1});
    write_record_field_<12>(header, uint32_t{1});

    const ::fixy::View<std::byte, TRACE_FIRST_OP_BYTES> op =
        image.view().window<TRACE_FIRST_OP_BYTES>(kOpAt).expect("the op fits the image");
    write_record_field_<0>(op, kSchema);
    write_record_field_<8>(op, kShape);
    write_record_field_<16>(op, kScope);
    write_record_field_<24>(op, kCallsite);
    write_record_field_<32>(op, int64_t{-9});
    write_record_field_<72>(op, uint16_t{0});
    write_record_field_<74>(op, uint16_t{1});
    write_record_field_<76>(op, uint16_t{1});
    write_record_field_<78>(op, uint8_t{1});

    const ::fixy::View<std::byte, TRACE_FIRST_META_BYTES> meta =
        image.view().window<TRACE_FIRST_META_BYTES>(kMetaAt).expect("the meta fits the image");
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

    const LoadedTrace trace =
        load_image(image, ::fixy::CText{"crtrace-first-format.bin"}).expect("a trace of the first format loads");
    CRUCIBLE_TEST_ASSERT(kTest, trace.num_ops == 1 && trace.num_metas == 1);
    const TraceRing::Entry& entry = *trace.entries.get(0).expect("the trace holds one op");
    CRUCIBLE_TEST_ASSERT(kTest, entry.schema_hash.raw() == kSchema && entry.shape_hash.raw() == kShape);
    CRUCIBLE_TEST_ASSERT(kTest, entry.num_inputs == 0 && entry.num_outputs == 1 && entry.num_scalar_args == 1);
    CRUCIBLE_TEST_ASSERT(kTest, entry.scalar_values.at(::fixy::index<0>()) == -9);
    CRUCIBLE_TEST_ASSERT(kTest, (entry.op_flags & op_flag::GRAD_ENABLED) != 0);
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
    ::fixy::FixedArray<std::byte, kBytes> image{};
    const ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header_bytes = encode_trace_header(1, 0);
    const ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> op_bytes = encode_trace_op(record);
    ::fixy::copy(image.view().window<TRACE_HEADER_BYTES>(0).expect("the header fits the image"), header_bytes.view());
    ::fixy::copy(image.view().window<sizeof(TraceOpRecord)>(kOpAt).expect("the op fits the image"), op_bytes.view());
    constexpr ::fixy::CText kFileName{"crtrace-one-op.bin"};
    const auto checked = [](::fixy::TextView text) noexcept {
        return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(text))
            .expect("a plain relative path passes the check");
    };
    const ::fixy::fs::DirectoryHandle here =
        ::fixy::fs::open_directory(kTestIo, checked(::fixy::TextView{"."})).expect("the working directory opens");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    using TraceFile = ::fixy::fs::FileOf<WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(kTestIo, here.ref(), kFileName)
                                .match(
                                    [&image](::fixy::Linear<TraceFile> opened) noexcept {
                                        const TraceFile file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(kTestIo, file.ref(), image.view()).is_ok();
                                    },
                                    [](::foundation::os::Errno) noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    ::fixy::Result<LoadedTrace, TraceLoadError> loaded = load_trace(kTestIo, checked(kFileName.view()));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::unlink_at(kTestIo, here.ref(), kFileName).is_ok());
    return loaded;
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

// The trailing schema-name section of the file carries a length field
// gated by a refinement over one to 256 bytes.
//
// Each case clears the global table first, which also unseals it so a
// mutable view can be minted.
//
// The runtime guard and the refinement are a pair.  The guard means the
// refinement's constructor never sees an out-of-range value, and the
// refinement covers a reader that bypasses this loader.

// The scalar is appended in little-endian byte order.
template <typename T>
static void append_le(std::vector<unsigned char>& buf, T v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}

// A sixteen-byte header with both counts at zero, ready for a name-table
// append.
static std::vector<unsigned char> make_zero_op_header() {
    std::vector<unsigned char> buf;
    buf.reserve(16);
    const char magic[4] = {'C', 'R', 'T', 'R'};
    buf.insert(buf.end(), reinterpret_cast<const unsigned char*>(magic),
               reinterpret_cast<const unsigned char*>(magic) + 4);
    append_le<uint32_t>(buf, crucible::TRACE_FORMAT_VERSION);  // version
    append_le<uint32_t>(buf, 0);  // n_ops
    append_le<uint32_t>(buf, 0);  // n_metas
    return buf;
}

// The trace of trace_path gives aten::add, aten::mul and aten::matmul to
// three hashes.  The table holds a different name of the hash of aten::mul
// first.  Input from outside can be wrong, so the table keeps its first name
// and refuses aten::mul.  The load stops the names of the trace there, it
// reports the reason, and the process lives.  The load runs in a child, so
// the test reads the report on the standard error of the child.
static void
test_schema_name_table_collision_is_refused(::fixy::Path<::fixy::tags::source::Sanitized> const& trace_path) {
    global_schema_table().clear(::foundation::effects::testing::test());
    constexpr SchemaHash kFirstHash{0xAAAA111122223333ULL};
    constexpr SchemaHash kHeldHash{0xBBBB444455556666ULL};
    constexpr SchemaHash kLaterHash{0xCCCC777788889999ULL};
    for (const SchemaTable::MutableView& view : global_schema_table().mint_mutable_view(kVigilForeground)) {
        const bool was_registered = register_schema_name(kTestIo, kTestIo.cap().alloc, view, kHeldHash,
                                                         ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(
                                                             ::fixy::TextView{"aten::held"}))
                                        .is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, was_registered);
    }

    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(
                kTestIo,
                [&trace_path] noexcept {
                    const LoadedTrace loaded = load_trace(kTestIo, kVigilForeground, trace_path)
                                                   .expect("a colliding name ends the names of the trace only");
                    const bool keeps_earlier = is_named(global_schema_table().lookup(kFirstHash), "aten::add");
                    const bool keeps_held = is_named(global_schema_table().lookup(kHeldHash), "aten::held");
                    const bool stops_there = missing(global_schema_table().lookup(kLaterHash));
                    if (loaded.num_ops != 0 || !keeps_earlier || !keeps_held || !stops_there) {
                        ::fixy::fatal("the colliding name changed the table");
                    }
                })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ::fixy::find(report.output(),
                     ::fixy::TextView{"load_trace: the trace gives the schema hash 13527480966017541734 the name "
                                      "aten::mul, and the schema table holds a different name of that hash"})
            .is_some());
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(kHeldHash), "aten::held"));

    global_schema_table().clear(::foundation::effects::testing::test());
    ::fixy::check::pass(kTest, "  test_schema_name_table_collision_is_refused: PASSED\n");
}

// The trace of trace_path gives aten::add, aten::mul and aten::matmul to
// three hashes, and the table holds one name less than SCHEMA_TABLE_CAP
// first.  aten::add takes the last place.  A full table refuses aten::mul,
// because input from outside can name more schemas than the table holds.  The
// load stops the names of the trace there, it reports the reason, and the
// process lives.  The load runs in a child, so the test reads the report on
// the standard error of the child.
static void test_schema_name_table_full_is_refused(::fixy::Path<::fixy::tags::source::Sanitized> const& trace_path) {
    global_schema_table().clear(::foundation::effects::testing::test());
    constexpr SchemaHash kFirstHash{0xAAAA111122223333ULL};
    constexpr SchemaHash kRefusedHash{0xBBBB444455556666ULL};
    constexpr SchemaHash kLaterHash{0xCCCC777788889999ULL};
    for (const SchemaTable::MutableView& view : global_schema_table().mint_mutable_view(kVigilForeground)) {
        for (uint64_t filler = 1; filler < SCHEMA_TABLE_CAP; ++filler) {
            const bool was_registered = register_schema_name(kTestIo, kTestIo.cap().alloc, view, SchemaHash{filler},
                                                             ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(
                                                                 ::fixy::TextView{"aten::filler"}))
                                            .is_ok();
            CRUCIBLE_TEST_ASSERT(kTest, was_registered);
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == SCHEMA_TABLE_CAP - 1);

    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(
                kTestIo,
                [&trace_path] noexcept {
                    const LoadedTrace loaded = load_trace(kTestIo, kVigilForeground, trace_path)
                                                   .expect("a full schema table ends the names of the trace only");
                    const bool keeps_earlier = is_named(global_schema_table().lookup(kFirstHash), "aten::add");
                    const bool refuses_next = missing(global_schema_table().lookup(kRefusedHash));
                    const bool stops_there = missing(global_schema_table().lookup(kLaterHash));
                    const bool is_full_table = global_schema_table().count() == SCHEMA_TABLE_CAP;
                    if (loaded.num_ops != 0 || !keeps_earlier || !refuses_next || !stops_there || !is_full_table) {
                        ::fixy::fatal("the full schema table took a name past its last place");
                    }
                })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::find(report.output(),
                            ::fixy::TextView{"load_trace: the schema table holds 512 names, so it refuses the name "
                                             "aten::mul of the schema hash 13527480966017541734"})
                   .is_some());

    global_schema_table().clear(::foundation::effects::testing::test());
    ::fixy::check::pass(kTest, "  test_schema_name_table_full_is_refused: PASSED\n");
}

static void test_schema_name_table_round_trip() {
    global_schema_table().clear(::foundation::effects::testing::test());

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 3);  // num_names

    auto append_entry = [&](uint64_t hash, const char* name) {
        const uint16_t len = static_cast<uint16_t>(std::strlen(name));
        append_le<uint64_t>(buf, hash);
        append_le<uint16_t>(buf, len);
        buf.insert(buf.end(), reinterpret_cast<const unsigned char*>(name),
                   reinterpret_cast<const unsigned char*>(name) + len);
    };
    append_entry(0xAAAA111122223333ULL, "aten::add");
    append_entry(0xBBBB444455556666ULL, "aten::mul");
    append_entry(0xCCCC777788889999ULL, "aten::matmul");

    std::string path = write_tmp(buf.data(), buf.size());

    // A load with no producer context reads the names and registers none.
    CRUCIBLE_TEST_ASSERT(kTest, load_trace(kTestIo, sanitized(path)).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);

    const LoadedTrace t = load_trace(kTestIo, kVigilForeground, sanitized(path)).expect("a trace with names loads");

    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 0);

    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xAAAA111122223333ULL}), "aten::add"));
    CRUCIBLE_TEST_ASSERT(kTest, is_named(global_schema_table().lookup(SchemaHash{0xBBBB444455556666ULL}), "aten::mul"));
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_named(global_schema_table().lookup(SchemaHash{0xCCCC777788889999ULL}), "aten::matmul"));

    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0xDEADBEEFULL})));

    test_schema_name_table_collision_is_refused(sanitized(path));
    test_schema_name_table_full_is_refused(sanitized(path));
    std::remove(path.c_str());

    global_schema_table().clear(::foundation::effects::testing::test());
    ::fixy::check::pass(kTest, "  test_schema_name_table_round_trip: PASSED\n");
}

static void test_schema_name_table_corrupt_zero_len() {
    // A length of zero is the lower-bound corruption.  Without the
    // minimum, the hash would be bound to the empty string left by the
    // zero-initialized buffer, and every later lookup of that hash would
    // return it.
    global_schema_table().clear(::foundation::effects::testing::test());

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);  // num_names
    append_le<uint64_t>(buf, 0xDEADBEEFCAFEBABEULL);  // schema_hash
    append_le<uint16_t>(buf, 0);  // CORRUPT name_len

    std::string path = write_tmp(buf.data(), buf.size());
    // The header and ops parsed cleanly, so the loader still returns a
    // trace.  A failure in the name table breaks that loop only.
    const LoadedTrace t =
        load_trace(kTestIo, kVigilForeground, sanitized(path)).expect("a name of length zero ends the table only");
    std::remove(path.c_str());
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);

    // The guard fired before the registration, so the hash never
    // reached the table.
    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0xDEADBEEFCAFEBABEULL})));
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);

    global_schema_table().clear(::foundation::effects::testing::test());
    ::fixy::check::pass(kTest, "  test_schema_name_table_corrupt_zero_len: PASSED\n");
}

static void test_schema_name_table_corrupt_oversize_len() {
    // A length of 300 exceeds the 256-byte maximum.  Without the upper
    // bound the read would pull 300 bytes into a 257-byte buffer and the
    // trailing terminator would land past its end.  The guard must fire
    // before both the refinement and the read.
    //
    // The file carries 300 bytes of plausible name data, so that a
    // dropped bound would let the read succeed and the overrun actually
    // happen.  The bound promises to stop before reading, and that is
    // what the case checks.
    global_schema_table().clear(::foundation::effects::testing::test());

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);  // num_names
    append_le<uint64_t>(buf, 0xFEEDFACEBAADF00DULL);  // schema_hash
    append_le<uint16_t>(buf, 300);  // CORRUPT name_len
    // A recognizable byte, so it stands out in a buffer that should
    // never hold it.
    buf.insert(buf.end(), 300, static_cast<unsigned char>(0x5A));

    std::string path = write_tmp(buf.data(), buf.size());
    const LoadedTrace t = load_trace(kTestIo, kVigilForeground, sanitized(path))
                              .expect("a name past the length bound ends the table only");
    std::remove(path.c_str());
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 0);

    // The guard fired before the length was refined, which under
    // enforced contracts would otherwise abort.
    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0xFEEDFACEBAADF00DULL})));
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);

    global_schema_table().clear(::foundation::effects::testing::test());
    ::fixy::check::pass(kTest, "  test_schema_name_table_corrupt_oversize_len: PASSED\n");
}

// The loader builds each op's meta start from a running cursor over its
// tensor counts, and every consumer indexes the metas from that start.
// A cursor that runs past the meta count turns those reads into an
// out-of-bounds read driven by an untrusted file.  The loader must reject
// such a file, so that the trace it returns is always self-consistent.

// Build a 1-op .crtrace with the given num_metas and op tensor counts.
// The metas region is zero-filled (ndim=0, valid).  Returns the temp path.
static std::string write_one_op_trace(uint32_t n_metas, uint16_t num_inputs, uint16_t num_outputs) {
    std::vector<uint8_t> buf(16 + sizeof(TraceOpRecord) + static_cast<size_t>(n_metas) * TRACE_META_BYTES, 0);
    const char magic[4] = {'C', 'R', 'T', 'R'};
    const uint32_t version = crucible::TRACE_FORMAT_VERSION, n_ops = 1;
    std::memcpy(buf.data() + 0, magic, 4);
    std::memcpy(buf.data() + 4, &version, 4);
    std::memcpy(buf.data() + 8, &n_ops, 4);
    std::memcpy(buf.data() + 12, &n_metas, 4);
    // The op record begins at offset 16.  The two tensor counts sit at
    // +72 and +74 inside it.
    std::memcpy(buf.data() + 16 + 72, &num_inputs, 2);
    std::memcpy(buf.data() + 16 + 74, &num_outputs, 2);
    return write_tmp(buf.data(), buf.size());
}

static void test_meta_overrun_rejected() {
    // Op claims 5 tensors (3 in + 2 out) but only 2 metas exist — the
    // running meta cursor would overrun the metas vector.  Reject.
    std::string path = write_one_op_trace(/*n_metas=*/2, /*in=*/3, /*out=*/2);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::MetaRunPastCount));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_meta_overrun_rejected:     PASSED\n");
}

static void test_meta_exact_count_loads() {
    // Op claims exactly 5 tensors and 5 metas exist — in bounds, loads.
    std::string path = write_one_op_trace(/*n_metas=*/5, /*in=*/3, /*out=*/2);
    const LoadedTrace t = load_trace(kTestIo, sanitized(path)).expect("an op whose metadata fits the count loads");
    CRUCIBLE_TEST_ASSERT(kTest, t.num_ops == 1);
    CRUCIBLE_TEST_ASSERT(kTest, t.num_metas == 5);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->num_inputs == 3);
    CRUCIBLE_TEST_ASSERT(kTest, t.entries.get(0).expect("the trace holds one op")->num_outputs == 2);
    CRUCIBLE_TEST_ASSERT(kTest, t.meta_starts.get(0).expect("the trace holds one op")->raw()
                                    == 0);  // sole op starts at metas[0]
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_meta_exact_count_loads:    PASSED\n");
}

static void test_meta_count_uint16_overflow_rejected() {
    // The two counts sum to 65536, which wraps to zero in 16 bits.  Summed
    // in 32 bits it is a real claim of 65536 tensors, and the file holds
    // two metas.
    std::string path = write_one_op_trace(/*n_metas=*/2, /*in=*/65535, /*out=*/1);
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::MetaRunPastCount));
    std::remove(path.c_str());
    ::fixy::check::pass(kTest, "  test_meta_uint16_overflow:      PASSED\n");
}

// The peak resident size of this process, in kilobytes.  It only grows,
// so the case that reads it runs first.
static long peak_resident_kb() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

static void test_counts_past_the_file_allocate_nothing() {
    // Each count is under its cap, and the file holds the header alone.
    // A loader that sizes its vectors from the counts before it compares
    // them with the file touches hundreds of megabytes here.
    const long peak_before_kb = peak_resident_kb();
    for (const auto& [claimed_ops, claimed_metas] :
         {std::pair<uint32_t, uint32_t>{MAX_OPS, 0u}, std::pair<uint32_t, uint32_t>{0u, 1u << 20}}) {
        auto buf = make_zero_op_header();
        std::memcpy(buf.data() + 8, &claimed_ops, 4);
        std::memcpy(buf.data() + 12, &claimed_metas, 4);
        std::string path = write_tmp(buf.data(), buf.size());
        CRUCIBLE_TEST_ASSERT(kTest, is_refused_as(load_trace(kTestIo, sanitized(path)), TraceLoadError::CountPastFile));
        std::remove(path.c_str());
    }
    const long growth_kb = peak_resident_kb() - peak_before_kb;
    CRUCIBLE_TEST_ASSERT(kTest, growth_kb < 64 * 1024);
    ::fixy::check::pass(kTest, "  test_counts_past_the_file:      PASSED\n");
}

// One metadata record as the file carries it.  A record of zeros is a
// valid descriptor of rank zero.
using MetaRecord = ::fixy::FixedArray<uint8_t, sizeof(TensorMeta)>;

template <class Field>
static void set_meta_field(MetaRecord& record, std::size_t field_offset, Field value) {
    std::memcpy(&record.at(::fixy::Index<sizeof(TensorMeta)>::check(field_offset).expect("the field is in the record")),
                &value, sizeof(Field));
}

// Loads a trace of one op with one tensor, whose metadata is the record.
static bool loads_with_meta(const MetaRecord& record) {
    std::vector<uint8_t> buf(16 + sizeof(TraceOpRecord) + sizeof(TensorMeta), 0);
    const char magic[4] = {'C', 'R', 'T', 'R'};
    const uint32_t version = crucible::TRACE_FORMAT_VERSION, n_ops = 1, n_metas = 1;
    const uint16_t num_inputs = 1;
    std::memcpy(buf.data() + 0, magic, 4);
    std::memcpy(buf.data() + 4, &version, 4);
    std::memcpy(buf.data() + 8, &n_ops, 4);
    std::memcpy(buf.data() + 12, &n_metas, 4);
    std::memcpy(buf.data() + 16 + 72, &num_inputs, 2);
    std::memcpy(buf.data() + 16 + sizeof(TraceOpRecord), &record.at(::fixy::index<0>()), record.size());
    std::string path = write_tmp(buf.data(), buf.size());
    const auto loaded = load_trace(kTestIo, sanitized(path));
    std::remove(path.c_str());
    // Only the metadata record differs from a valid trace, so a refusal has
    // the one reason of a corrupt record.
    CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok() || is_refused_as(loaded, TraceLoadError::MetaCorrupt));
    return loaded.is_ok();
}

template <class Field>
static bool loads_with_meta_field(std::size_t field_offset, Field value) {
    MetaRecord record{};
    set_meta_field(record, field_offset, value);
    return loads_with_meta(record);
}

static void test_meta_bytes_break_no_claim_of_the_type() {
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta(MetaRecord{}));

    // A bool that holds any byte but zero or one is undefined to read.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field(offsetof(TensorMeta, requires_grad), uint8_t{2}));
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta_field(offsetof(TensorMeta, requires_grad), uint8_t{1}));

    // A size lane inside the rank must fit the extent bound.
    {
        MetaRecord rank_one{};
        set_meta_field(rank_one, offsetof(TensorMeta, ndim), uint8_t{1});
        set_meta_field(rank_one, offsetof(TensorMeta, sizes), kMaxTensorDimExtent);
        CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta(rank_one));
        set_meta_field(rank_one, offsetof(TensorMeta, sizes), std::numeric_limits<int64_t>::max());
        CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta(rank_one));
    }

    // A lane past the rank holds zero, so equal descriptors write equal
    // bytes.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field(offsetof(TensorMeta, sizes) + sizeof(int64_t) * 3, int64_t{5}));
    CRUCIBLE_TEST_ASSERT(kTest,
                         !loads_with_meta_field(offsetof(TensorMeta, strides) + sizeof(int64_t) * 7, int64_t{1}));

    // A flag bit that no meta_flags constant names is reserved.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field(offsetof(TensorMeta, flags), uint8_t{0x40}));
    CRUCIBLE_TEST_ASSERT(kTest, loads_with_meta_field(offsetof(TensorMeta, flags), meta_flags::IS_CONJ));

    // A device index below the host is not a device.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field(offsetof(TensorMeta, device_idx), int8_t{-2}));

    // A rank past the bound is refused before a lane is read.
    CRUCIBLE_TEST_ASSERT(kTest, !loads_with_meta_field(offsetof(TensorMeta, ndim), uint8_t{9}));
    ::fixy::check::pass(kTest, "  test_meta_bytes_checked:        PASSED\n");
}

static void test_names_on_a_sealed_table_are_skipped() {
    // The trace loads, and its names stay out of the sealed table.
    global_schema_table().clear(::foundation::effects::testing::test());
    global_schema_table().seal(kTest);

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);  // num_names
    append_le<uint64_t>(buf, 0x5EA1ED0000000001ULL);
    const char name[] = "aten::sealed";
    append_le<uint16_t>(buf, static_cast<uint16_t>(sizeof(name) - 1));
    buf.insert(buf.end(), name, name + sizeof(name) - 1);

    std::string path = write_tmp(buf.data(), buf.size());
    const bool is_loaded = load_trace(kTestIo, kVigilForeground, sanitized(path)).is_ok();
    std::remove(path.c_str());

    CRUCIBLE_TEST_ASSERT(kTest, is_loaded);
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().is_sealed());
    CRUCIBLE_TEST_ASSERT(kTest, global_schema_table().count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, missing(global_schema_table().lookup(SchemaHash{0x5EA1ED0000000001ULL})));

    global_schema_table().clear(::foundation::effects::testing::test());
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
    test_schema_name_table_round_trip();
    test_schema_name_table_corrupt_zero_len();
    test_schema_name_table_corrupt_oversize_len();
    test_meta_overrun_rejected();
    test_meta_exact_count_loads();
    test_meta_count_uint16_overflow_rejected();
    test_meta_bytes_break_no_claim_of_the_type();
    test_names_on_a_sealed_table_are_skipped();
    ::fixy::check::pass(kTest, "test_trace_loader: 21 groups, all passed\n");
    return 0;
}
