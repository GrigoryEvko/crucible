// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-075: ctx-bound split requires the same ExecCtx to admit the
// parent and every child permission row.
//
// Violation: BgCompileCtx admits IO but not Block.  Splitting a
// Row<IO, Block> parent into IO and Block children under BgCompileCtx
// must fail because the Block child cannot enter that context.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/permissions/Permission.h>

#include <utility>

namespace neg_perm_split_row_unifies_strict {

struct Whole {};
struct IoChild {};
struct BlockChild {};

}  // namespace neg_perm_split_row_unifies_strict

namespace crucible::safety {

template <>
struct permission_row<neg_perm_split_row_unifies_strict::Whole> {
    using type = ::crucible::effects::Row<
        ::crucible::effects::Effect::IO,
        ::crucible::effects::Effect::Block>;
};

template <>
struct permission_row<neg_perm_split_row_unifies_strict::IoChild> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

template <>
struct permission_row<neg_perm_split_row_unifies_strict::BlockChild> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
};

template <>
struct splits_into<
    neg_perm_split_row_unifies_strict::Whole,
    neg_perm_split_row_unifies_strict::IoChild,
    neg_perm_split_row_unifies_strict::BlockChild> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_perm_split_row_unifies_strict;
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;

    auto whole = saf::mint_permission_root<tags::Whole>(eff::TestRunnerCtx{});
    auto split = saf::mint_permission_split<tags::IoChild, tags::BlockChild>(
        eff::BgCompileCtx{},
        std::move(whole));
    saf::permission_drop(std::move(split.first));
    saf::permission_drop(std::move(split.second));
    return 0;
}

