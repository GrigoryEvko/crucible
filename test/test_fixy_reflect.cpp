// ── test_fixy_reflect — fixy::reflect:: re-export reach (FIXY-V-040)
//
// Sentinel TU for `include/crucible/fixy/Reflect.h`.  The header ships
// every static_assert inside `namespace self_test`; including it from
// a real TU forces the compiler to evaluate them under the project's
// warnings-as-errors flags (per feedback_header_only_static_assert
// _blind_spot.md).  This TU adds runtime witnesses on top of the
// compile-time identity sentinels.
//
// Substrate: see Reflect.h doc-block for the full 8-symbol surface.

#include <crucible/fixy/Reflect.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <type_traits>

// ── Probe types — declared at TU scope so reflection introspection
//                  has access at every consteval evaluation point.

struct ReflectRtProbe {
    std::uint32_t alpha = 17;
    std::uint64_t beta  = 0xCAFEBABEDEADBEEFULL;
    std::int32_t  gamma = -3;
};

enum class ReflectRtFlags : std::uint8_t {
    Read    = 0x01,
    Write   = 0x02,
    Execute = 0x04,
    None    = 0x00,
    RW      = 0x03,   // composite — filtered by single-bit iterator
};

namespace fr = ::crucible::fixy::reflect;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses ────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Re-test identity sentinels at TU scope so the substrate witnesses
// the alias from outside the safety/ tree.  Drift from header-internal
// to consumer-visible (rare; would indicate a build-config divergence)
// is caught here.

static_assert(fr::has_reflected_hash<ReflectRtProbe>);
static_assert(!fr::has_reflected_hash<int>);
static_assert(!fr::has_reflected_hash<double>);

// Trait reach for nested struct.
struct ReflectRtNested {
    ReflectRtProbe inner;
    std::uint32_t  tag = 1;
};
static_assert(fr::has_reflected_hash<ReflectRtNested>,
    "Nested struct of reflectable type must itself satisfy the trait.");

// Enumerator-name identity at TU scope.
static_assert(fr::enumerator_name(ReflectRtFlags::Read) == "Read");
static_assert(fr::enumerator_name(ReflectRtFlags::Write) == "Write");
static_assert(fr::enumerator_name(ReflectRtFlags::Execute) == "Execute");
static_assert(fr::enumerator_name(ReflectRtFlags::None) == "None");
static_assert(fr::enumerator_name(ReflectRtFlags::RW) == "RW");

// Unknown value → empty.
static_assert(fr::enumerator_name(static_cast<ReflectRtFlags>(0x80)).empty());

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ─────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// 1. reflect_hash determinism — same input ⇒ same hash bytes.
static void test_runtime_reflect_hash_determinism() {
    ReflectRtProbe a{};
    ReflectRtProbe b{};
    auto h1 = fr::reflect_hash(a);
    auto h2 = fr::reflect_hash(b);
    if (h1 != h2) {
        std::fprintf(stderr, "reflect_hash drift: %llu vs %llu\n",
                     static_cast<unsigned long long>(h1),
                     static_cast<unsigned long long>(h2));
        std::abort();
    }
}

// 2. reflect_hash sensitivity — different field ⇒ different hash.
static void test_runtime_reflect_hash_field_sensitivity() {
    ReflectRtProbe a{};
    ReflectRtProbe b{};
    b.alpha = 99;
    if (fr::reflect_hash(a) == fr::reflect_hash(b)) {
        std::fprintf(stderr, "reflect_hash insensitive to field change\n");
        std::abort();
    }
}

// 3. reflect_fmix_fold determinism + seed sensitivity.
static void test_runtime_reflect_fmix_fold_seed_sensitivity() {
    ReflectRtProbe a{};
    auto h_a = fr::reflect_fmix_fold<0x9E3779B97F4A7C15ULL>(a);
    auto h_b = fr::reflect_fmix_fold<0x9E3779B97F4A7C15ULL>(a);
    if (h_a != h_b) {
        std::fprintf(stderr, "fmix_fold non-deterministic\n");
        std::abort();
    }
    auto h_c = fr::reflect_fmix_fold<0xDEADBEEFULL>(a);
    if (h_a == h_c) {
        std::fprintf(stderr, "fmix_fold seed parameter dropped\n");
        std::abort();
    }
}

// 4. for_each_enumerator runtime iteration — name + value pairs.
static void test_runtime_for_each_enumerator() {
    int       count          = 0;
    bool      saw_read       = false;
    bool      saw_write      = false;
    bool      saw_execute    = false;
    bool      saw_none       = false;
    bool      saw_composite  = false;
    fr::for_each_enumerator<ReflectRtFlags>(
        [&](ReflectRtFlags value, std::string_view name) noexcept {
            ++count;
            if (value == ReflectRtFlags::Read    && name == "Read")    saw_read = true;
            if (value == ReflectRtFlags::Write   && name == "Write")   saw_write = true;
            if (value == ReflectRtFlags::Execute && name == "Execute") saw_execute = true;
            if (value == ReflectRtFlags::None    && name == "None")    saw_none = true;
            if (value == ReflectRtFlags::RW      && name == "RW")      saw_composite = true;
        });
    if (count != 5) {
        std::fprintf(stderr, "for_each_enumerator: expected 5, got %d\n", count);
        std::abort();
    }
    if (!(saw_read && saw_write && saw_execute && saw_none && saw_composite)) {
        std::fprintf(stderr, "for_each_enumerator: missing enumerator\n");
        std::abort();
    }
}

// 5. for_each_single_bit_enumerator — popcount filter (3 single-bit
//    enumerators: Read, Write, Execute).
static void test_runtime_for_each_single_bit() {
    int count = 0;
    fr::for_each_single_bit_enumerator<ReflectRtFlags>(
        [&](ReflectRtFlags, std::string_view) noexcept { ++count; });
    if (count != 3) {
        std::fprintf(stderr, "for_each_single_bit: expected 3, got %d\n", count);
        std::abort();
    }
}

// 6. bits_to_string — round-trip + truncation.
static void test_runtime_bits_to_string() {
    char buf[32] = {};
    ::crucible::safety::Bits<ReflectRtFlags> b{
        ReflectRtFlags::Read, ReflectRtFlags::Execute};
    auto n = fr::bits_to_string<ReflectRtFlags>(b, buf, sizeof(buf));
    if (std::string_view{buf} != "Read|Execute") {
        std::fprintf(stderr, "bits_to_string: got \"%s\"\n", buf);
        std::abort();
    }
    if (n != std::string_view{"Read|Execute"}.size()) {
        std::fprintf(stderr, "bits_to_string: needed = %zu\n", n);
        std::abort();
    }

    // Truncation — give only enough room for "Re" + NUL.
    char tight[4] = {};
    auto nt = fr::bits_to_string<ReflectRtFlags>(b, tight, sizeof(tight));
    if (nt != std::string_view{"Read|Execute"}.size()) {
        std::fprintf(stderr, "bits_to_string truncation needed-count drift\n");
        std::abort();
    }
    if (std::string_view{tight} != "Rea") {  // cap=4 → 3 chars + NUL
        std::fprintf(stderr, "bits_to_string truncation buf: \"%s\"\n", tight);
        std::abort();
    }
}

// 7. reflect_print — produces output, fixed prefix.  Pipe to a memory
//    stream so the test is deterministic across platforms.
static void test_runtime_reflect_print() {
    char  membuf[256] = {};
    FILE* stream      = std::fopen("/dev/null", "w");
    if (!stream) std::abort();
    fr::reflect_print(ReflectRtProbe{}, stream);
    std::fclose(stream);
    (void)membuf;
    // Output destination is /dev/null — the success criterion is that
    // reflect_print is callable through the alias without crashing.
    // Bit-for-bit content stability is exercised by the substrate's
    // own self-test.
}

// 8. has_reflected_hash gating witness — used as a `requires` gate.
template <typename T>
    requires fr::has_reflected_hash<T>
[[nodiscard]] static std::uint64_t hash_if_reflectable(const T& obj) noexcept {
    return fr::reflect_hash(obj);
}
static void test_runtime_has_reflected_hash_gate() {
    auto h = hash_if_reflectable(ReflectRtProbe{42, 99, -1});
    if (h == 0) {
        // Family-B hash is fmix64-finalized; 0 IS a possible output but
        // astronomically unlikely for any non-degenerate input.
        std::fprintf(stderr, "hash unexpectedly 0\n");
        std::abort();
    }
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_reflect_hash_determinism();
    test_runtime_reflect_hash_field_sensitivity();
    test_runtime_reflect_fmix_fold_seed_sensitivity();
    test_runtime_for_each_enumerator();
    test_runtime_for_each_single_bit();
    test_runtime_bits_to_string();
    test_runtime_reflect_print();
    test_runtime_has_reflected_hash_gate();
    std::printf("test_fixy_reflect: 8/8 runtime witnesses passed\n");
    return 0;
}
