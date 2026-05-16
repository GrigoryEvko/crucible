// ── test_fixy_reflect — FIXY-G8 positive test ─────────────────────────
//
// Exercise reflect_grade + stable_grade_hash against:
//   * AllStrict binding (all 20 dims accept_default_strict_for)
//   * CopyUsage binding (Usage relaxed via grant::copy)
//   * Multi-relax CntpFrame-shape binding (Effect + Provenance +
//     Representation + Complexity + Space + Mutation relaxed)
//   * MimicHook-shape binding (Effect + Trust + Representation relaxed
//     via grant::vendor_nv + tier_bitexact)
//
// Pins:
//   * descriptor.dims[i].dim_name matches dim-name table at each i
//   * stable_grade_hash != 0 for engaged bindings
//   * grade-different bindings produce distinct hashes
//   * grade-equivalent bindings produce equal hashes
//   * is_relaxed flag correctly distinguishes accept_default_strict_for
//     from grant::* relaxation tags

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

// ── Worked example shape 1: all-strict (every dim accepted strict) ──
using AllStrictFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Worked example shape 2: CntpFrame (multi-relax) ──
using CntpFrameFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,                                              // Usage
    cg::with<fx::Effect::IO>,                              // Effect
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cg::from_source<::crucible::safety::source::External>, // Provenance
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_portable,                                   // Representation
    cf::accept_default_strict_for<cd::Observability>,
    cg::complexity_linear<1>,                              // Complexity
    cf::accept_default_strict_for<cd::Precision>,
    cg::space_bounded<2048>,                               // Space
    cf::accept_default_strict_for<cd::Overflow>,
    cg::append_only,                                       // Mutation
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Worked example shape 3: MimicHook (Mimic-vendor backend) ──
using MimicHookFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,                                              // Usage
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,           // Effect
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_nv,                                         // Representation
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,                                     // Precision
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,                                  // Mutation
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Equivalent-to-AllStrictFn (same grade vector, different Type carrier).
// stable_grade_hash should distinguish them ONLY via the type identity
// path — but reflect_grade is purely about the 20-dim grade, not the
// Type itself.  So two fixy::fn<T1, same-grants> and
// fixy::fn<T2, same-grants> should produce the SAME stable_grade_hash
// because the grade is grant-driven.
using AllStrictDouble = cf::fn<double,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ─── Compile-time invariants ────────────────────────────────────────

constexpr auto all_strict = cf::reflect_grade<AllStrictFn>();
constexpr auto cntp       = cf::reflect_grade<CntpFrameFn>();
constexpr auto mimic_hook = cf::reflect_grade<MimicHookFn>();
constexpr auto all_strict_double = cf::reflect_grade<AllStrictDouble>();

static_assert(all_strict.dims.size() == cf::GRADE_DIM_COUNT);

// Every dim_name in canonical order.
static_assert(all_strict.dims[0].dim_name  == "Type");
static_assert(all_strict.dims[1].dim_name  == "Refinement");
static_assert(all_strict.dims[2].dim_name  == "Usage");
static_assert(all_strict.dims[3].dim_name  == "Effect");
static_assert(all_strict.dims[4].dim_name  == "Security");
static_assert(all_strict.dims[5].dim_name  == "Protocol");
static_assert(all_strict.dims[6].dim_name  == "Lifetime");
static_assert(all_strict.dims[7].dim_name  == "Provenance");
static_assert(all_strict.dims[8].dim_name  == "Trust");
static_assert(all_strict.dims[9].dim_name  == "Representation");
static_assert(all_strict.dims[10].dim_name == "Observability");
static_assert(all_strict.dims[11].dim_name == "Complexity");
static_assert(all_strict.dims[12].dim_name == "Precision");
static_assert(all_strict.dims[13].dim_name == "Space");
static_assert(all_strict.dims[14].dim_name == "Overflow");
static_assert(all_strict.dims[15].dim_name == "Mutation");
static_assert(all_strict.dims[16].dim_name == "Reentrancy");
static_assert(all_strict.dims[17].dim_name == "Size");
static_assert(all_strict.dims[18].dim_name == "Version");
static_assert(all_strict.dims[19].dim_name == "Staleness");

// All-strict: every is_relaxed=false, grant_name == "strict-default".
static_assert(!all_strict.dims[0].is_relaxed);
static_assert(all_strict.dims[0].grant_name == "strict-default");
static_assert(!all_strict.dims[19].is_relaxed);
static_assert(all_strict.dims[19].grant_name == "strict-default");

// CntpFrame: Usage / Effect / Provenance / Representation / Complexity /
// Space / Mutation are relaxed; Type / Refinement / Security / Protocol /
// Lifetime / Trust / Observability / Precision / Overflow / Reentrancy /
// Size / Version / Staleness remain strict.
static_assert(cntp.dims[2].is_relaxed,  "Usage relaxed via grant::copy");
static_assert(cntp.dims[3].is_relaxed,  "Effect relaxed via grant::with<IO>");
static_assert(cntp.dims[7].is_relaxed,  "Provenance relaxed via grant::from_source");
static_assert(cntp.dims[9].is_relaxed,  "Representation relaxed via grant::vendor_portable");
static_assert(cntp.dims[11].is_relaxed, "Complexity relaxed via grant::complexity_linear");
static_assert(cntp.dims[13].is_relaxed, "Space relaxed via grant::space_bounded");
static_assert(cntp.dims[15].is_relaxed, "Mutation relaxed via grant::append_only");
static_assert(!cntp.dims[0].is_relaxed,  "Type strict");
static_assert(!cntp.dims[4].is_relaxed,  "Security strict");
static_assert(!cntp.dims[8].is_relaxed,  "Trust strict");

// Stable hash is non-zero for every engaged binding.
static_assert(all_strict.stable_hash != 0);
static_assert(cntp.stable_hash       != 0);
static_assert(mimic_hook.stable_hash != 0);

// Grade-different bindings produce distinct hashes (collision
// probability ~2^-32; this assertion holds for shipped grade shapes).
static_assert(all_strict.stable_hash != cntp.stable_hash,
    "AllStrict and CntpFrame have different grade vectors; hashes must differ.");
static_assert(all_strict.stable_hash != mimic_hook.stable_hash,
    "AllStrict and MimicHook have different grade vectors; hashes must differ.");
static_assert(cntp.stable_hash       != mimic_hook.stable_hash,
    "CntpFrame and MimicHook differ on Effect / Representation / Mutation; "
    "hashes must differ.");

// Grade-equivalent bindings (same Grants pack, different Type carrier)
// produce equal hashes — reflect_grade is grant-driven, not type-driven.
static_assert(all_strict.stable_hash == all_strict_double.stable_hash,
    "fixy::fn<T1, same-grants> and fixy::fn<T2, same-grants> share the "
    "same 20-dim grade vector; stable_grade_hash must agree.");

// stable_grade_hash<F> matches the computed descriptor.stable_hash.
static_assert(cf::stable_grade_hash<AllStrictFn> == all_strict.stable_hash);
static_assert(cf::stable_grade_hash<CntpFrameFn> == cntp.stable_hash);
static_assert(cf::stable_grade_hash<MimicHookFn> == mimic_hook.stable_hash);

// CntpFrame's grant_name on dim::Usage is the stable name of grant::copy.
// We don't pin the exact string (compiler-dependent display name) but
// require it to NOT be "strict-default" since Usage is relaxed.
static_assert(cntp.dims[2].grant_name != std::string_view{"strict-default"});

}  // namespace

int main() {
    // Runtime smoke — exercise the constexpr surface with non-constant
    // inputs to satisfy feedback_algebra_runtime_smoke_test_discipline.
    auto desc1 = cf::reflect_grade<AllStrictFn>();
    auto desc2 = cf::reflect_grade<CntpFrameFn>();
    auto desc3 = cf::reflect_grade<MimicHookFn>();

    std::uint64_t hash_sink = 0;
    hash_sink ^= desc1.stable_hash;
    hash_sink ^= desc2.stable_hash;
    hash_sink ^= desc3.stable_hash;
    hash_sink ^= cf::stable_grade_hash<AllStrictFn>;
    hash_sink ^= cf::stable_grade_hash<CntpFrameFn>;
    hash_sink ^= cf::stable_grade_hash<MimicHookFn>;

    // Per-dim name iteration — verify the runtime loop produces the
    // canonical sequence.
    static constexpr std::array<std::string_view, 20> expected_names{{
        "Type", "Refinement", "Usage", "Effect", "Security",
        "Protocol", "Lifetime", "Provenance", "Trust", "Representation",
        "Observability", "Complexity", "Precision", "Space", "Overflow",
        "Mutation", "Reentrancy", "Size", "Version", "Staleness",
    }};
    for (std::size_t i = 0; i < desc1.dims.size(); ++i) {
        if (desc1.dims[i].dim_name != expected_names[i]) {
            std::fprintf(stderr,
                "name mismatch at i=%zu: got=%.*s expected=%.*s\n",
                i,
                static_cast<int>(desc1.dims[i].dim_name.size()),
                desc1.dims[i].dim_name.data(),
                static_cast<int>(expected_names[i].size()),
                expected_names[i].data());
            return 1;
        }
    }

    // Force `hash_sink` to be observed so the loads don't fold away.
    if (hash_sink == 0xDEAD'BEEF'CAFE'BABEULL) {
        std::fprintf(stderr, "improbable hash sink value: %lx\n",
                     static_cast<unsigned long>(hash_sink));
        return 2;
    }

    std::fputs("test_fixy_reflect: OK\n", stdout);
    return 0;
}
