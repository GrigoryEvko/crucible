// Assertions embedded in a header are only verified under the
// project's warning flags when some translation unit includes that
// header.  This file exists to be that translation unit for the two
// harvests that introspect a function's parameter list, one for
// effect rows and one for permission tags.

#include <crucible/fixy/wrap/Inferred.h>

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/InferredRow.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace fw = ::crucible::fixy::wrap;
namespace extract = ::crucible::safety::extract;
namespace effects = ::crucible::effects;

namespace probes {

void f_pure(int, double) noexcept;

void f_alloc(effects::Alloc, std::size_t) noexcept;

// A context parameter contributes its own atom.  The harvest reads
// the literal parameter type, so it does not expand a context into
// the row that context stands for.
void f_bg(effects::Bg, int) noexcept;

void f_init(effects::Init, int) noexcept;

void f_alloc_io(effects::Alloc, effects::IO, int) noexcept;

void f_alloc_io_block(effects::Alloc, effects::IO, effects::Block) noexcept;

void f_alloc_dup(effects::Alloc, effects::Alloc, int) noexcept;

void f_no_tags(int, double, char*) noexcept;

void f_nullary() noexcept;

}  // namespace probes

static_assert(std::is_same_v<fw::inferred_row_t<&probes::f_pure>, extract::inferred_row_t<&probes::f_pure>>);
static_assert(std::is_same_v<fw::inferred_row_t<&probes::f_pure>, effects::EmptyRow>);
static_assert(std::is_same_v<fw::inferred_row_t<&probes::f_alloc>, effects::Row<effects::Effect::Alloc>>);
static_assert(
    std::is_same_v<fw::inferred_row_t<&probes::f_alloc_io>, effects::Row<effects::Effect::Alloc, effects::Effect::IO>>);
static_assert(std::is_same_v<fw::inferred_row_t<&probes::f_alloc_io_block>,
                             effects::Row<effects::Effect::Alloc, effects::Effect::IO, effects::Effect::Block>>);

static_assert(fw::inferred_row_count_v<&probes::f_pure> == 0);
static_assert(fw::inferred_row_count_v<&probes::f_alloc> == 1);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_io> == 2);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_io_block> == 3);
static_assert(fw::inferred_row_count_v<&probes::f_alloc_dup> == 1);

static_assert(fw::function_has_effect_v<&probes::f_alloc, effects::Effect::Alloc>);
static_assert(!fw::function_has_effect_v<&probes::f_alloc, effects::Effect::IO>);
static_assert(fw::function_has_effect_v<&probes::f_alloc_io, effects::Effect::IO>);
static_assert(fw::function_has_effect_v<&probes::f_bg, effects::Effect::Bg>);
static_assert(fw::function_has_effect_v<&probes::f_init, effects::Effect::Init>);
static_assert(!fw::function_has_effect_v<&probes::f_pure, effects::Effect::Alloc>);

static_assert(fw::is_pure_function_v<&probes::f_pure>);
static_assert(!fw::is_pure_function_v<&probes::f_alloc>);
static_assert(!fw::is_pure_function_v<&probes::f_bg>);
static_assert(fw::IsPureFunction<&probes::f_pure>);
static_assert(!fw::IsPureFunction<&probes::f_alloc_io>);

static_assert(fw::IsPureFunction<&probes::f_pure> == extract::IsPureFunction<&probes::f_pure>);

static_assert(
    std::is_same_v<fw::inferred_permission_tags_t<&probes::f_no_tags>, ::crucible::safety::proto::EmptyPermSet>);
static_assert(
    std::is_same_v<fw::inferred_permission_tags_raw_t<&probes::f_no_tags>, ::crucible::safety::proto::EmptyPermSet>);
// A capability tag is not a permission tag, so a function carrying
// one still harvests no permission tags at all.
static_assert(
    std::is_same_v<fw::inferred_permission_tags_t<&probes::f_alloc>, ::crucible::safety::proto::EmptyPermSet>);

static_assert(fw::inferred_permission_tags_count_v<&probes::f_no_tags> == 0);
static_assert(fw::inferred_permission_tags_count_v<&probes::f_nullary> == 0);
static_assert(fw::inferred_permission_tags_count_v<&probes::f_alloc> == 0);

static_assert(fw::is_tag_free_function_v<&probes::f_no_tags>);
static_assert(fw::is_tag_free_function_v<&probes::f_nullary>);
static_assert(fw::is_tag_free_function_v<&probes::f_alloc>);
static_assert(fw::is_tag_free_function_v<&probes::f_alloc_io>);

static_assert(fw::IsTagFreeFunction<&probes::f_no_tags>);
static_assert(fw::IsTagFreeFunction<&probes::f_alloc>);

static_assert(fw::IsTagFreeFunction<&probes::f_no_tags> == extract::IsTagFreeFunction<&probes::f_no_tags>);

static void test_runtime_row_count() {
    volatile std::size_t pure_n = fw::inferred_row_count_v<&probes::f_pure>;
    volatile std::size_t alloc_n = fw::inferred_row_count_v<&probes::f_alloc>;
    volatile std::size_t two_n = fw::inferred_row_count_v<&probes::f_alloc_io>;
    volatile std::size_t three_n = fw::inferred_row_count_v<&probes::f_alloc_io_block>;
    if (pure_n != 0) std::abort();
    if (alloc_n != 1) std::abort();
    if (two_n != 2) std::abort();
    if (three_n != 3) std::abort();
}

static void test_runtime_has_effect() {
    volatile bool a = fw::function_has_effect_v<&probes::f_alloc, effects::Effect::Alloc>;
    volatile bool b = fw::function_has_effect_v<&probes::f_alloc, effects::Effect::IO>;
    volatile bool c = fw::function_has_effect_v<&probes::f_bg, effects::Effect::Bg>;
    if (!a) std::abort();
    if (b) std::abort();
    if (!c) std::abort();
}

static void test_runtime_is_pure() {
    volatile bool pure = fw::is_pure_function_v<&probes::f_pure>;
    volatile bool impure = fw::is_pure_function_v<&probes::f_alloc>;
    if (!pure) std::abort();
    if (impure) std::abort();
}

static void test_runtime_perm_tags_count() {
    volatile std::size_t a = fw::inferred_permission_tags_count_v<&probes::f_no_tags>;
    volatile std::size_t b = fw::inferred_permission_tags_count_v<&probes::f_nullary>;
    volatile std::size_t c = fw::inferred_permission_tags_count_v<&probes::f_alloc>;
    if (a != 0) std::abort();
    if (b != 0) std::abort();
    if (c != 0) std::abort();
}

static void test_runtime_is_tag_free() {
    volatile bool t1 = fw::is_tag_free_function_v<&probes::f_no_tags>;
    volatile bool t2 = fw::is_tag_free_function_v<&probes::f_alloc>;
    volatile bool t3 = fw::is_tag_free_function_v<&probes::f_alloc_io>;
    if (!t1) std::abort();
    if (!t2) std::abort();
    if (!t3) std::abort();
}

static void test_runtime_substrate_smoke_calls() {
    if (!extract::inferred_row_smoke_test()) std::abort();
    if (!extract::inferred_permission_tags_smoke_test()) std::abort();
}

// The two harvests are orthogonal.  One sees capability tags and no
// permission tags, the other sees permission tags and no capability
// tags.
static void test_runtime_axes_orthogonality() {
    volatile std::size_t r_alloc = fw::inferred_row_count_v<&probes::f_alloc>;
    volatile std::size_t p_alloc = fw::inferred_permission_tags_count_v<&probes::f_alloc>;
    if (r_alloc != 1) std::abort();
    if (p_alloc != 0) std::abort();

    volatile std::size_t r_none = fw::inferred_row_count_v<&probes::f_no_tags>;
    volatile std::size_t p_none = fw::inferred_permission_tags_count_v<&probes::f_no_tags>;
    if (r_none != 0) std::abort();
    if (p_none != 0) std::abort();
}

// A context parameter yields its own atom and nothing else.
static void test_runtime_bg_atom_not_expanded() {
    volatile bool has_bg = fw::function_has_effect_v<&probes::f_bg, effects::Effect::Bg>;
    volatile bool has_alloc = fw::function_has_effect_v<&probes::f_bg, effects::Effect::Alloc>;
    volatile bool has_io = fw::function_has_effect_v<&probes::f_bg, effects::Effect::IO>;
    volatile bool has_block = fw::function_has_effect_v<&probes::f_bg, effects::Effect::Block>;
    if (!has_bg) std::abort();
    if (has_alloc) std::abort();
    if (has_io) std::abort();
    if (has_block) std::abort();
}

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
