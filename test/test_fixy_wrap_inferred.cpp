// ── test_fixy_wrap_inferred — V-044 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/wrap/Inferred.h` under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), adds runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the two parameter-list-introspecting substrates:
//   * InferredRow              — Met(X) effect-row harvest
//   * InferredPermissionTags   — CSL permission-tag harvest

#include <crucible/fixy/wrap/Inferred.h>

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/InferredRow.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace fw      = ::crucible::fixy::wrap;
namespace extract = ::crucible::safety::extract;
namespace effects = ::crucible::effects;

// ═══════════════════════════════════════════════════════════════════
// ── Probe functions ──────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// Pure — no cap parameters, no permission tags.
void f_pure(int, double) noexcept;

// Single cap-tag parameter — Row<Alloc>.
void f_alloc(effects::Alloc, std::size_t) noexcept;

// Bg context (carries Alloc/IO/Block via row union of its row_type,
// but the InferredRow harvest sees the literal type Bg and inserts
// the `Bg` atom — NOT auto-expanded).
void f_bg(effects::Bg, int) noexcept;

// Init context.
void f_init(effects::Init, int) noexcept;

// Two distinct caps in declaration order.
void f_alloc_io(effects::Alloc, effects::IO, int) noexcept;

// All three primitive caps.
void f_alloc_io_block(effects::Alloc, effects::IO, effects::Block) noexcept;

// Duplicated cap — collapsed via insert-unique.
void f_alloc_dup(effects::Alloc, effects::Alloc, int) noexcept;

// Tag-free, cap-free function.
void f_no_tags(int, double, char*) noexcept;

// Nullary.
void f_nullary() noexcept;

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. inferred_row_t — type identity across reach paths ─────────

static_assert(std::is_same_v<
    fw::inferred_row_t<&probes::f_pure>,
    extract::inferred_row_t<&probes::f_pure>>);
static_assert(std::is_same_v<
    fw::inferred_row_t<&probes::f_pure>,
    effects::EmptyRow>);
static_assert(std::is_same_v<
    fw::inferred_row_t<&probes::f_alloc>,
    effects::Row<effects::Effect::Alloc>>);
static_assert(std::is_same_v<
    fw::inferred_row_t<&probes::f_alloc_io>,
    effects::Row<effects::Effect::Alloc, effects::Effect::IO>>);
static_assert(std::is_same_v<
    fw::inferred_row_t<&probes::f_alloc_io_block>,
    effects::Row<effects::Effect::Alloc, effects::Effect::IO,
                 effects::Effect::Block>>);

// ── 2. inferred_row_count_v — value + cross-path identity ────────

static_assert(fw::inferred_row_count_v<&probes::f_pure>           == 0);
static_assert(fw::inferred_row_count_v<&probes::f_alloc>          == 1);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_io>       == 2);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_io_block> == 3);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_dup>      == 1);

// ── 3. function_has_effect_v — point queries ─────────────────────

static_assert( fw::function_has_effect_v<&probes::f_alloc,
                                          effects::Effect::Alloc>);
static_assert(!fw::function_has_effect_v<&probes::f_alloc,
                                          effects::Effect::IO>);
static_assert( fw::function_has_effect_v<&probes::f_alloc_io,
                                          effects::Effect::IO>);
static_assert( fw::function_has_effect_v<&probes::f_bg,
                                          effects::Effect::Bg>);
static_assert( fw::function_has_effect_v<&probes::f_init,
                                          effects::Effect::Init>);
static_assert(!fw::function_has_effect_v<&probes::f_pure,
                                          effects::Effect::Alloc>);

// ── 4. is_pure_function_v + IsPureFunction concept ───────────────

static_assert( fw::is_pure_function_v<&probes::f_pure>);
static_assert(!fw::is_pure_function_v<&probes::f_alloc>);
static_assert(!fw::is_pure_function_v<&probes::f_bg>);
static_assert( fw::IsPureFunction<&probes::f_pure>);
static_assert(!fw::IsPureFunction<&probes::f_alloc_io>);

static_assert(
    fw::IsPureFunction<&probes::f_pure> ==
    extract::IsPureFunction<&probes::f_pure>);

// ── 5. inferred_permission_tags_t — empty for tag-free probes ────

static_assert(std::is_same_v<
    fw::inferred_permission_tags_t<&probes::f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);
static_assert(std::is_same_v<
    fw::inferred_permission_tags_raw_t<&probes::f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);
// Cap-tagged functions still produce empty permission tags — the
// two harvests are orthogonal.
static_assert(std::is_same_v<
    fw::inferred_permission_tags_t<&probes::f_alloc>,
    ::crucible::safety::proto::EmptyPermSet>);

// ── 6. inferred_permission_tags_count_v ──────────────────────────

static_assert(fw::inferred_permission_tags_count_v<&probes::f_no_tags> == 0);
static_assert(fw::inferred_permission_tags_count_v<&probes::f_nullary> == 0);
static_assert(fw::inferred_permission_tags_count_v<&probes::f_alloc>   == 0);

// ── 7. is_tag_free_function_v + IsTagFreeFunction concept ────────

static_assert( fw::is_tag_free_function_v<&probes::f_no_tags>);
static_assert( fw::is_tag_free_function_v<&probes::f_nullary>);
static_assert( fw::is_tag_free_function_v<&probes::f_alloc>);
static_assert( fw::is_tag_free_function_v<&probes::f_alloc_io>);

static_assert( fw::IsTagFreeFunction<&probes::f_no_tags>);
static_assert( fw::IsTagFreeFunction<&probes::f_alloc>);

static_assert(
    fw::IsTagFreeFunction<&probes::f_no_tags> ==
    extract::IsTagFreeFunction<&probes::f_no_tags>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// 1. inferred_row_count_v readback through alias.
static void test_runtime_row_count() {
    volatile std::size_t pure_n  = fw::inferred_row_count_v<&probes::f_pure>;
    volatile std::size_t alloc_n = fw::inferred_row_count_v<&probes::f_alloc>;
    volatile std::size_t two_n   = fw::inferred_row_count_v<&probes::f_alloc_io>;
    volatile std::size_t three_n = fw::inferred_row_count_v<&probes::f_alloc_io_block>;
    if (pure_n  != 0) std::abort();
    if (alloc_n != 1) std::abort();
    if (two_n   != 2) std::abort();
    if (three_n != 3) std::abort();
}

// 2. function_has_effect_v point query through alias.
static void test_runtime_has_effect() {
    volatile bool a = fw::function_has_effect_v<&probes::f_alloc,
                                                  effects::Effect::Alloc>;
    volatile bool b = fw::function_has_effect_v<&probes::f_alloc,
                                                  effects::Effect::IO>;
    volatile bool c = fw::function_has_effect_v<&probes::f_bg,
                                                  effects::Effect::Bg>;
    if (!a) std::abort();
    if ( b) std::abort();
    if (!c) std::abort();
}

// 3. is_pure_function_v through alias.
static void test_runtime_is_pure() {
    volatile bool pure   = fw::is_pure_function_v<&probes::f_pure>;
    volatile bool impure = fw::is_pure_function_v<&probes::f_alloc>;
    if (!pure)   std::abort();
    if ( impure) std::abort();
}

// 4. inferred_permission_tags_count_v through alias.
static void test_runtime_perm_tags_count() {
    volatile std::size_t a = fw::inferred_permission_tags_count_v<&probes::f_no_tags>;
    volatile std::size_t b = fw::inferred_permission_tags_count_v<&probes::f_nullary>;
    volatile std::size_t c = fw::inferred_permission_tags_count_v<&probes::f_alloc>;
    if (a != 0) std::abort();
    if (b != 0) std::abort();
    if (c != 0) std::abort();
}

// 5. is_tag_free_function_v through alias.
static void test_runtime_is_tag_free() {
    volatile bool t1 = fw::is_tag_free_function_v<&probes::f_no_tags>;
    volatile bool t2 = fw::is_tag_free_function_v<&probes::f_alloc>;
    volatile bool t3 = fw::is_tag_free_function_v<&probes::f_alloc_io>;
    if (!t1) std::abort();
    if (!t2) std::abort();
    if (!t3) std::abort();
}

// 6. Substrate smoke-test invocation through extract::.
static void test_runtime_substrate_smoke_calls() {
    if (!extract::inferred_row_smoke_test())              std::abort();
    if (!extract::inferred_permission_tags_smoke_test())  std::abort();
}

// 7. Axes orthogonality — InferredRow detects cap-tags, NOT
//    permission tags; InferredPermissionTags detects permission
//    tags, NOT cap-tags.
static void test_runtime_axes_orthogonality() {
    // f_alloc — cap-tag axis says 1, permission-tag axis says 0.
    volatile std::size_t r_alloc = fw::inferred_row_count_v<&probes::f_alloc>;
    volatile std::size_t p_alloc = fw::inferred_permission_tags_count_v<&probes::f_alloc>;
    if (r_alloc != 1) std::abort();
    if (p_alloc != 0) std::abort();

    // f_no_tags — both axes say 0.
    volatile std::size_t r_none = fw::inferred_row_count_v<&probes::f_no_tags>;
    volatile std::size_t p_none = fw::inferred_permission_tags_count_v<&probes::f_no_tags>;
    if (r_none != 0) std::abort();
    if (p_none != 0) std::abort();
}

// 8. Bg context resolves as `Bg` atom, NOT as Alloc/IO/Block — the
//    InferredRow harvest looks at literal parameter type, not the
//    context's expanded row_type.
static void test_runtime_bg_atom_not_expanded() {
    volatile bool has_bg    = fw::function_has_effect_v<&probes::f_bg,
                                                          effects::Effect::Bg>;
    volatile bool has_alloc = fw::function_has_effect_v<&probes::f_bg,
                                                          effects::Effect::Alloc>;
    volatile bool has_io    = fw::function_has_effect_v<&probes::f_bg,
                                                          effects::Effect::IO>;
    volatile bool has_block = fw::function_has_effect_v<&probes::f_bg,
                                                          effects::Effect::Block>;
    if (!has_bg)    std::abort();
    if ( has_alloc) std::abort();
    if ( has_io)    std::abort();
    if ( has_block) std::abort();
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_row_count();
    test_runtime_has_effect();
    test_runtime_is_pure();
    test_runtime_perm_tags_count();
    test_runtime_is_tag_free();
    test_runtime_substrate_smoke_calls();
    test_runtime_axes_orthogonality();
    test_runtime_bg_atom_not_expanded();
    std::printf("test_fixy_wrap_inferred: 8/8 runtime witnesses passed\n");
    return 0;
}
