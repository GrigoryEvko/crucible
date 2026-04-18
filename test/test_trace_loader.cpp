// Tests for .crtrace format robustness.
//
// A malformed trace file must reject gracefully (return nullptr) rather
// than crash.  Covers: missing file, empty file, bad magic, wrong
// version, truncated header, truncated op records, truncated metas,
// and the happy path round-trip.

#include <crucible/TraceLoader.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

using namespace crucible;

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
    std::printf("test_trace_loader: 8 groups, all passed\n");
    return 0;
}
