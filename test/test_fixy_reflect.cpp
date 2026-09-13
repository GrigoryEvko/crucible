// The header keeps its static_asserts inside a self-test namespace.
// They are evaluated under the project warnings-as-errors flags only
// when a translation unit in the build graph includes the header, so
// the include below is itself part of the claim.  The runtime
// witnesses are layered on top of those compile-time sentinels.

#include <crucible/fixy/Reflect.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <type_traits>

// The probe types live at translation-unit scope so that reflection
// introspection can reach them from every consteval evaluation point.

struct ReflectRtProbe {
    std::uint32_t alpha = 17;
    std::uint64_t beta = 0xCAFEBABEDEADBEEFULL;
    std::int32_t gamma = -3;
};

enum class ReflectRtFlags : std::uint8_t {
    Read = 0x01,
    Write = 0x02,
    Execute = 0x04,
    None = 0x00,
    RW = 0x03,  // composite — filtered by single-bit iterator
};

namespace fr = ::crucible::fixy::reflect;

// These repeat sentinels the header already carries.  Re-running them
// from a consumer translation unit catches a build-configuration
// divergence that leaves a trait visible inside the header and not
// outside it.

static_assert(fr::has_reflected_hash<ReflectRtProbe>);
static_assert(!fr::has_reflected_hash<int>);
static_assert(!fr::has_reflected_hash<double>);

struct ReflectRtNested {
    ReflectRtProbe inner;
    std::uint32_t tag = 1;
};
static_assert(fr::has_reflected_hash<ReflectRtNested>,
              "Nested struct of reflectable type must itself satisfy the trait.");

static_assert(fr::enumerator_name(ReflectRtFlags::Read) == "Read");
static_assert(fr::enumerator_name(ReflectRtFlags::Write) == "Write");
static_assert(fr::enumerator_name(ReflectRtFlags::Execute) == "Execute");
static_assert(fr::enumerator_name(ReflectRtFlags::None) == "None");
static_assert(fr::enumerator_name(ReflectRtFlags::RW) == "RW");

static_assert(fr::enumerator_name(static_cast<ReflectRtFlags>(0x80)).empty());

static void test_runtime_reflect_hash_determinism() {
    ReflectRtProbe a{};
    ReflectRtProbe b{};
    auto h1 = fr::reflect_hash(a);
    auto h2 = fr::reflect_hash(b);
    if (h1 != h2) {
        std::fprintf(stderr, "reflect_hash drift: %llu vs %llu\n", static_cast<unsigned long long>(h1),
                     static_cast<unsigned long long>(h2));
        std::abort();
    }
}

static void test_runtime_reflect_hash_field_sensitivity() {
    ReflectRtProbe a{};
    ReflectRtProbe b{};
    b.alpha = 99;
    if (fr::reflect_hash(a) == fr::reflect_hash(b)) {
        std::fprintf(stderr, "reflect_hash insensitive to field change\n");
        std::abort();
    }
}

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

static void test_runtime_for_each_enumerator() {
    int count = 0;
    bool saw_read = false;
    bool saw_write = false;
    bool saw_execute = false;
    bool saw_none = false;
    bool saw_composite = false;
    fr::for_each_enumerator<ReflectRtFlags>([&](ReflectRtFlags value, std::string_view name) noexcept {
        ++count;
        if (value == ReflectRtFlags::Read && name == "Read") saw_read = true;
        if (value == ReflectRtFlags::Write && name == "Write") saw_write = true;
        if (value == ReflectRtFlags::Execute && name == "Execute") saw_execute = true;
        if (value == ReflectRtFlags::None && name == "None") saw_none = true;
        if (value == ReflectRtFlags::RW && name == "RW") saw_composite = true;
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

// Read, Write and Execute are the only single-bit enumerators, so the
// popcount filter leaves three.
static void test_runtime_for_each_single_bit() {
    int count = 0;
    fr::for_each_single_bit_enumerator<ReflectRtFlags>([&](ReflectRtFlags, std::string_view) noexcept { ++count; });
    if (count != 3) {
        std::fprintf(stderr, "for_each_single_bit: expected 3, got %d\n", count);
        std::abort();
    }
}

static void test_runtime_bits_to_string() {
    char buf[32] = {};
    ::crucible::safety::Bits<ReflectRtFlags> b{ReflectRtFlags::Read, ReflectRtFlags::Execute};
    auto n = fr::bits_to_string<ReflectRtFlags>(b, buf, sizeof(buf));
    if (std::string_view{buf} != "Read|Execute") {
        std::fprintf(stderr, "bits_to_string: got \"%s\"\n", buf);
        std::abort();
    }
    if (n != std::string_view{"Read|Execute"}.size()) {
        std::fprintf(stderr, "bits_to_string: needed = %zu\n", n);
        std::abort();
    }

    // A four-byte buffer holds three characters and the NUL, and the
    // needed-count must still report the untruncated length.
    char tight[4] = {};
    auto nt = fr::bits_to_string<ReflectRtFlags>(b, tight, sizeof(tight));
    if (nt != std::string_view{"Read|Execute"}.size()) {
        std::fprintf(stderr, "bits_to_string truncation needed-count drift\n");
        std::abort();
    }
    if (std::string_view{tight} != "Rea") {
        std::fprintf(stderr, "bits_to_string truncation buf: \"%s\"\n", tight);
        std::abort();
    }
}

static void test_runtime_reflect_print() {
    char membuf[256] = {};
    FILE* stream = std::fopen("/dev/null", "w");
    if (!stream) std::abort();
    fr::reflect_print(ReflectRtProbe{}, stream);
    std::fclose(stream);
    (void)membuf;
    // The output goes to /dev/null.  The claim is only that the call
    // is reachable through the alias and returns, because content
    // stability is already covered by the header's own self-test.
}

template <typename T>
    requires fr::has_reflected_hash<T>
[[nodiscard]] static std::uint64_t hash_if_reflectable(const T& obj) noexcept {
    return fr::reflect_hash(obj);
}
static void test_runtime_has_reflected_hash_gate() {
    auto h = hash_if_reflectable(ReflectRtProbe{42, 99, -1});
    if (h == 0) {
        // Zero is a legal output of the finalizer, so this is a smoke
        // check against a hash that never ran, not a hard invariant.
        std::fprintf(stderr, "hash unexpectedly 0\n");
        std::abort();
    }
}

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
