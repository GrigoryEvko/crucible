// Tests for .crtrace format robustness.
//
// A malformed trace file must reject gracefully (return nullptr) rather
// than crash.  Covers: missing file, empty file, bad magic, wrong
// version, truncated header, truncated op records, truncated metas,
// and the happy path round-trip.

#include <crucible/TraceLoader.h>
#include <crucible/SchemaTable.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace crucible;

static const char* C(SchemaTable::LookupName name) {
    return name.value().data();
}

static bool missing(SchemaTable::LookupName name) {
    return name.value().data() == nullptr;
}

// Write raw bytes to a temp file; caller removes when done.
// Unique path via getpid + a monotonic counter — tmpnam() is deprecated
// and GCC warns warn_unused_result on the fallback signature.
static std::string write_tmp(const void* data, size_t n) {
    static int ctr = 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/tmp/crtrace-%d-%d.bin",
                  ::getpid(), ++ctr);
    std::FILE* f = std::fopen(buf, "wb");
    assert(f);
    std::fwrite(data, 1, n, f);
    std::fclose(f);
    return buf;
}

static void test_missing_file() {
    auto t = load_trace("/definitely/does/not/exist.crtrace");
    assert(!t);
    std::printf("  test_missing_file:              PASSED\n");
}

static void test_empty_file() {
    std::string path = write_tmp("", 0);
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_empty_file:                PASSED\n");
}

static void test_bad_magic() {
    // 16 bytes: wrong magic + valid-looking counts.
    struct { char magic[4]; uint32_t version; uint32_t n_ops; uint32_t n_metas; }
    hdr{.magic = {'X','X','X','X'}, .version = 1, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_bad_magic:                 PASSED\n");
}

static void test_wrong_version() {
    struct { char magic[4]; uint32_t version; uint32_t n_ops; uint32_t n_metas; }
    hdr{.magic = {'C','R','T','R'}, .version = 99, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_wrong_version:             PASSED\n");
}

static void test_truncated_header() {
    // Only 8 bytes — missing n_ops + n_metas.
    char buf[8] = {'C', 'R', 'T', 'R', 1, 0, 0, 0};
    std::string path = write_tmp(buf, sizeof(buf));
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_truncated_header:          PASSED\n");
}

static void test_truncated_op_records() {
    // Header claims 5 ops but file has only header + 40 bytes (half an op).
    struct { char magic[4]; uint32_t version; uint32_t n_ops; uint32_t n_metas; }
    hdr{.magic = {'C','R','T','R'}, .version = 1, .n_ops = 5, .n_metas = 0};
    char buf[16 + 40] = {};
    std::memcpy(buf, &hdr, sizeof(hdr));
    std::string path = write_tmp(buf, sizeof(buf));
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_truncated_ops:             PASSED\n");
}

static void test_happy_path_zero_ops() {
    // Minimal valid file: 16-byte header with n_ops=0 n_metas=0.
    struct { char magic[4]; uint32_t version; uint32_t n_ops; uint32_t n_metas; }
    hdr{.magic = {'C','R','T','R'}, .version = 1, .n_ops = 0, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    auto t = load_trace(path.c_str());
    assert(t);
    assert(t->num_ops == 0);
    assert(t->num_metas == 0);
    assert(t->entries.empty());
    std::remove(path.c_str());
    std::printf("  test_happy_path_empty:          PASSED\n");
}

static void test_adversarial_num_ops_rejected() {
    // Header claiming 4 G + 1 ops must be rejected before allocating
    // a 320 GB std::vector<TraceOpRecord>.
    struct { char magic[4]; uint32_t version; uint32_t n_ops; uint32_t n_metas; }
    hdr{.magic = {'C','R','T','R'}, .version = 1,
        .n_ops = 0xFFFF'FFFFu, .n_metas = 0};
    std::string path = write_tmp(&hdr, sizeof(hdr));
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_adversarial_counts:        PASSED\n");
}

static void test_round_trip_single_op() {
    // Build a minimal valid file: header + 1 op record (80B) + 0 metas.
    char buf[16 + 80] = {};
    const char magic[4] = {'C','R','T','R'};
    const uint32_t version = 1, n_ops = 1, n_metas = 0;
    std::memcpy(buf + 0,  magic, 4);
    std::memcpy(buf + 4,  &version,  4);
    std::memcpy(buf + 8,  &n_ops,    4);
    std::memcpy(buf + 12, &n_metas,  4);

    const uint64_t schema   = 0xAABB'CCDD'EEFF'0011ULL;
    const uint64_t shape    = 0x1122'3344'5566'7788ULL;
    const uint64_t scope    = 0xDEAD'BEEF'CAFE'BABEULL;
    const uint64_t callsite = 0xFEED'FACE'0000'00ADULL;
    std::memcpy(buf + 16 + 0,  &schema,   8);
    std::memcpy(buf + 16 + 8,  &shape,    8);
    std::memcpy(buf + 16 + 16, &scope,    8);
    std::memcpy(buf + 16 + 24, &callsite, 8);
    // scalar_values[5] zero, num_inputs=0, num_outputs=0, num_scalars=0
    // grad_enabled=0, op_flags=0 — all zero-filled.

    std::string path = write_tmp(buf, sizeof(buf));
    auto t = load_trace(path.c_str());
    std::remove(path.c_str());
    assert(t);
    assert(t->num_ops == 1);
    assert(t->entries[0].schema_hash.raw() == schema);
    assert(t->entries[0].shape_hash.raw()  == shape);
    assert(t->scope_hashes[0].raw()        == scope);
    assert(t->callsite_hashes[0].raw()     == callsite);
    std::printf("  test_round_trip_single_op:      PASSED\n");
}

// ── Schema-name table round-trip + corruption resistance ───────────
//
// WRAP-TraceLoader-3 (#1051) introduces ValidSchemaNameLen — the
// Refined<in_range<1, 256>, uint16_t> gate at the .crtrace name-length
// boundary.  These tests close the audit gap left by that change: the
// existing 9 tests cover header / op-record / meta paths but never
// exercise the *trailing* schema-name section where the gate sits.
//
// Each test calls global_schema_table().clear() for state isolation
// (clear() resets sealed_ to false, so mint_mutable_view() succeeds).
//
// The corruption tests prove the if-break + Refined gate are the
// defense-in-depth pair: the runtime guard never reaches the Refined
// ctor with an out-of-range value (so the contract pre-clause holds),
// AND the Refined gate stops aggregate-init / future-reader paths that
// might bypass load_trace entirely.

// Append little-endian bytes of a trivially-copyable scalar.
template <typename T>
static void append_le(std::vector<unsigned char>& buf, T v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}

// Build a .crtrace prefix: 16 B header with n_ops=0, n_metas=0.
// Returns the buffer ready for a name-table append.
static std::vector<unsigned char> make_zero_op_header() {
    std::vector<unsigned char> buf;
    buf.reserve(16);
    const char magic[4] = {'C','R','T','R'};
    buf.insert(buf.end(),
               reinterpret_cast<const unsigned char*>(magic),
               reinterpret_cast<const unsigned char*>(magic) + 4);
    append_le<uint32_t>(buf, 1);   // version
    append_le<uint32_t>(buf, 0);   // n_ops
    append_le<uint32_t>(buf, 0);   // n_metas
    return buf;
}

static void test_schema_name_table_round_trip() {
    global_schema_table().clear();

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 3);  // num_names

    auto append_entry = [&](uint64_t hash, const char* name) {
        const uint16_t len = static_cast<uint16_t>(std::strlen(name));
        append_le<uint64_t>(buf, hash);
        append_le<uint16_t>(buf, len);
        buf.insert(buf.end(),
                   reinterpret_cast<const unsigned char*>(name),
                   reinterpret_cast<const unsigned char*>(name) + len);
    };
    append_entry(0xAAAA'1111'2222'3333ULL, "aten::add");
    append_entry(0xBBBB'4444'5555'6666ULL, "aten::mul");
    append_entry(0xCCCC'7777'8888'9999ULL, "aten::matmul");

    std::string path = write_tmp(buf.data(), buf.size());
    auto t = load_trace(path.c_str());
    std::remove(path.c_str());

    assert(t);
    assert(t->num_ops == 0);
    assert(t->num_metas == 0);

    const char* n1 = C(global_schema_table().lookup(
        SchemaHash{0xAAAA'1111'2222'3333ULL}));
    const char* n2 = C(global_schema_table().lookup(
        SchemaHash{0xBBBB'4444'5555'6666ULL}));
    const char* n3 = C(global_schema_table().lookup(
        SchemaHash{0xCCCC'7777'8888'9999ULL}));
    assert(n1 && std::strcmp(n1, "aten::add") == 0);
    assert(n2 && std::strcmp(n2, "aten::mul") == 0);
    assert(n3 && std::strcmp(n3, "aten::matmul") == 0);

    // Unknown hash returns nullptr.
    assert(missing(global_schema_table().lookup(SchemaHash{0xDEAD'BEEFULL})));

    global_schema_table().clear();
    std::printf("  test_schema_name_table_round_trip: PASSED\n");
}

static void test_schema_name_table_corrupt_zero_len() {
    // Lower-bound corruption: name_len = 0.  Without the
    // SCHEMA_NAME_LEN_MIN gate, this would associate the schema_hash
    // with the empty string ("\0" via name_buf zero-init), poisoning
    // every later lookup of that hash with a useless empty name.
    global_schema_table().clear();

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);                                // num_names
    append_le<uint64_t>(buf, 0xDEAD'BEEF'CAFE'BABEULL);         // schema_hash
    append_le<uint16_t>(buf, 0);                                // CORRUPT name_len

    std::string path = write_tmp(buf.data(), buf.size());
    auto t = load_trace(path.c_str());
    std::remove(path.c_str());

    // Loader still returns a valid LoadedTrace (header + ops parsed
    // cleanly; name-table partial-failure breaks the loop without
    // tearing down the rest of the trace).
    assert(t);
    assert(t->num_ops == 0);

    // The hash was NOT registered — the if-break fired before the
    // register_schema_name call.
    assert(missing(global_schema_table().lookup(
        SchemaHash{0xDEAD'BEEF'CAFE'BABEULL})));
    assert(global_schema_table().count() == 0);

    global_schema_table().clear();
    std::printf("  test_schema_name_table_corrupt_zero_len: PASSED\n");
}

static void test_schema_name_table_corrupt_oversize_len() {
    // Upper-bound corruption: name_len = 300 > SCHEMA_NAME_LEN_MAX (256).
    // Without the upper-bound gate, the fread would attempt to read
    // 300 bytes into the 257-byte name_buf and the trailing
    // `name_buf[name_len] = '\0'` would write past the buffer's end —
    // heap-buffer-overflow under ASan, silent stack corruption
    // otherwise.  The if-break MUST fire before the Refined ctor and
    // before the fread.
    //
    // We pad with 300 bytes of plausible name data so that, if the
    // bound were silently dropped, the fread would succeed and the
    // overrun would actually happen.  The bound's contract is "stop
    // before reading", so the test verifies the read NEVER occurred.
    global_schema_table().clear();

    auto buf = make_zero_op_header();
    append_le<uint32_t>(buf, 1);                                // num_names
    append_le<uint64_t>(buf, 0xFEED'FACE'BAAD'F00DULL);         // schema_hash
    append_le<uint16_t>(buf, 300);                              // CORRUPT name_len
    // 300 bytes of padding — would-be-name-data if the bound were
    // bypassed.  Use a recognizable byte so a future debugger can spot
    // it in a buffer that shouldn't contain it.
    buf.insert(buf.end(), 300, static_cast<unsigned char>(0x5A));

    std::string path = write_tmp(buf.data(), buf.size());
    auto t = load_trace(path.c_str());
    std::remove(path.c_str());

    assert(t);
    assert(t->num_ops == 0);

    // The hash was NOT registered — the if-break fired before the
    // make_schema_name_len call (which would otherwise hit the Refined
    // ctor's pre clause and abort under contracts=enforce).
    assert(missing(global_schema_table().lookup(
        SchemaHash{0xFEED'FACE'BAAD'F00DULL})));
    assert(global_schema_table().count() == 0);

    global_schema_table().clear();
    std::printf("  test_schema_name_table_corrupt_oversize_len: PASSED\n");
}

// ── Meta-range overrun resistance ────────────────────────────────────
//
// load_trace builds meta_starts[i] from a running cursor over each op's
// (num_inputs + num_outputs).  Every consumer (BackgroundThread build
// path, vis/BlockDetector) indexes metas[meta_start + k]; if the cursor
// runs past num_metas, those reads overrun the metas vector — a heap OOB
// read from an untrusted .crtrace.  load_trace must reject such input so
// the LoadedTrace it returns is always self-consistent.

// Build a 1-op .crtrace with the given num_metas and op tensor counts.
// The metas region is zero-filled (ndim=0, valid).  Returns the temp path.
static std::string write_one_op_trace(uint32_t n_metas,
                                      uint16_t num_inputs,
                                      uint16_t num_outputs) {
    std::vector<uint8_t> buf(16 + 80 + static_cast<size_t>(n_metas) * 168, 0);
    const char magic[4] = {'C','R','T','R'};
    const uint32_t version = 1, n_ops = 1;
    std::memcpy(buf.data() + 0,  magic,    4);
    std::memcpy(buf.data() + 4,  &version, 4);
    std::memcpy(buf.data() + 8,  &n_ops,   4);
    std::memcpy(buf.data() + 12, &n_metas, 4);
    // Op record begins at offset 16; num_inputs @ +72, num_outputs @ +74.
    std::memcpy(buf.data() + 16 + 72, &num_inputs,  2);
    std::memcpy(buf.data() + 16 + 74, &num_outputs, 2);
    return write_tmp(buf.data(), buf.size());
}

static void test_meta_overrun_rejected() {
    // Op claims 5 tensors (3 in + 2 out) but only 2 metas exist — the
    // running meta cursor would overrun the metas vector.  Reject.
    std::string path = write_one_op_trace(/*n_metas=*/2, /*in=*/3, /*out=*/2);
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_meta_overrun_rejected:     PASSED\n");
}

static void test_meta_exact_count_loads() {
    // Op claims exactly 5 tensors and 5 metas exist — in bounds, loads.
    std::string path = write_one_op_trace(/*n_metas=*/5, /*in=*/3, /*out=*/2);
    auto t = load_trace(path.c_str());
    assert(t);
    assert(t->num_ops == 1);
    assert(t->num_metas == 5);
    assert(t->entries[0].num_inputs  == 3);
    assert(t->entries[0].num_outputs == 2);
    assert(t->meta_starts[0].raw() == 0);  // sole op starts at metas[0]
    std::remove(path.c_str());
    std::printf("  test_meta_exact_count_loads:    PASSED\n");
}

static void test_meta_count_uint16_overflow_rejected() {
    // 65535 + 1 = 65536 wraps to 0 if computed in uint16; computed in
    // uint32 it is a genuine 65536-tensor claim that overruns num_metas=2.
    std::string path = write_one_op_trace(/*n_metas=*/2, /*in=*/65535, /*out=*/1);
    auto t = load_trace(path.c_str());
    assert(!t);
    std::remove(path.c_str());
    std::printf("  test_meta_uint16_overflow:      PASSED\n");
}

int main() {
    test_missing_file();
    test_empty_file();
    test_bad_magic();
    test_wrong_version();
    test_truncated_header();
    test_truncated_op_records();
    test_happy_path_zero_ops();
    test_adversarial_num_ops_rejected();
    test_round_trip_single_op();
    test_schema_name_table_round_trip();
    test_schema_name_table_corrupt_zero_len();
    test_schema_name_table_corrupt_oversize_len();
    test_meta_overrun_rejected();
    test_meta_exact_count_loads();
    test_meta_count_uint16_overflow_rejected();
    std::printf("test_trace_loader: 15 groups, all passed\n");
    return 0;
}
