// Tests of foundation/core/Identity.h: mint_identity, Identity and
// UniqueIdentity.

#include <foundation/core/Identity.h>

#include <foundation/Platform.h>

#include <utility>

namespace {

using ::foundation::core::Identity;
using ::foundation::core::mint_identity;
using ::foundation::core::UniqueIdentity;

// A copy is equal to its source, and two mints are never equal.
void test_copy_and_fresh() {
    Identity const first = mint_identity();
    Identity const copy = first;
    Identity const second = mint_identity();
    CRUCIBLE_FATAL_INVARIANT(first == copy);
    CRUCIBLE_FATAL_INVARIANT(!(first == second));
    CRUCIBLE_FATAL_INVARIANT(!(copy == second));
}

// The property: in a run of mints, each identity is equal to itself, and to
// neither the first identity nor the identity before it.  An assignment
// keeps the identity of its source.
void test_each_mint_is_fresh() {
    Identity const anchor = mint_identity();
    Identity previous = mint_identity();
    for (int turn = 0; turn < 4096; ++turn) {
        Identity const next = mint_identity();
        CRUCIBLE_FATAL_INVARIANT(next == next);
        CRUCIBLE_FATAL_INVARIANT(!(next == previous));
        CRUCIBLE_FATAL_INVARIANT(!(next == anchor));
        previous = next;
        CRUCIBLE_FATAL_INVARIANT(previous == next);
    }
}

// Moves the right out under the first name, and gives true when the old name
// names the Identity.  Two parameters name one right, so the rule
// use-after-move does not see that the old name moved.
[[nodiscard]] bool names_under_the_old_name(UniqueIdentity& right, UniqueIdentity& old_name,
                                            Identity const& identity) noexcept {
    UniqueIdentity const moved{std::move(right)};
    CRUCIBLE_FATAL_INVARIANT(moved.names(identity));
    return old_name.names(identity);
}

// A right names the Identity that it was made from, and no other one.  A move
// gives it to the target and leaves none in the source.
void test_move_leaves_none() {
    Identity const identity = mint_identity();
    Identity const other = mint_identity();
    UniqueIdentity right{identity};
    CRUCIBLE_FATAL_INVARIANT(right.names(identity) && !right.names(other));
    CRUCIBLE_FATAL_INVARIANT(!names_under_the_old_name(right, right, identity));

    UniqueIdentity target{other};
    UniqueIdentity source{identity};
    target = std::move(source);
    CRUCIBLE_FATAL_INVARIANT(target.names(identity) && !target.names(other));
}

// Moves the right out under the first name, and gives true when the old name
// holds an Identity.
[[nodiscard]] bool is_held_under_the_old_name(UniqueIdentity& right, UniqueIdentity& old_name) noexcept {
    UniqueIdentity const moved{std::move(right)};
    CRUCIBLE_FATAL_INVARIANT(moved.is_held());
    return old_name.is_held();
}

// Two rights of one mint are a pair.  Two rights of two mints are none, and two
// emptied rights are none.
[[nodiscard]] bool pair_under_the_old_names(UniqueIdentity& first, UniqueIdentity& old_first, UniqueIdentity& second,
                                            UniqueIdentity& old_second) noexcept {
    UniqueIdentity const moved_first{std::move(first)};
    UniqueIdentity const moved_second{std::move(second)};
    CRUCIBLE_FATAL_INVARIANT(moved_first.pairs_with(moved_second));
    return old_first.pairs_with(old_second);
}

void test_pairs() {
    Identity const identity = mint_identity();
    UniqueIdentity first{identity};
    UniqueIdentity second{identity};
    UniqueIdentity const other{mint_identity()};
    CRUCIBLE_FATAL_INVARIANT(first.is_held() && first.pairs_with(second) && second.pairs_with(first));
    CRUCIBLE_FATAL_INVARIANT(!first.pairs_with(other) && !other.pairs_with(first));
    CRUCIBLE_FATAL_INVARIANT(!pair_under_the_old_names(first, first, second, second));

    UniqueIdentity third{identity};
    CRUCIBLE_FATAL_INVARIANT(!is_held_under_the_old_name(third, third));
}

// The assignment takes the Identity out of the source before it writes the
// target, so an assignment of a right to itself keeps it.
void assign_under_two_names(UniqueIdentity& right, UniqueIdentity& other_name) noexcept {
    right = std::move(other_name);
}

void test_self_move_keeps() {
    Identity const identity = mint_identity();
    UniqueIdentity right{identity};
    assign_under_two_names(right, right);
    CRUCIBLE_FATAL_INVARIANT(right.names(identity));
}

// Empties the right under the first name, and counts the fresh Identities that
// the old name names.
[[nodiscard]] int count_named_by_an_emptied_right(UniqueIdentity& right, UniqueIdentity& old_name) noexcept {
    UniqueIdentity const moved{std::move(right)};
    static_cast<void>(moved);
    int named = 0;
    for (int turn = 0; turn < 4096; ++turn) {
        if (old_name.names(mint_identity())) ++named;
    }
    return named;
}

// The property: no mint gives the word of an emptied right.
void test_emptied_right_names_no_identity() {
    UniqueIdentity right{mint_identity()};
    CRUCIBLE_FATAL_INVARIANT(count_named_by_an_emptied_right(right, right) == 0);
}

}  // namespace

int main() {
    test_copy_and_fresh();
    test_each_mint_is_fresh();
    test_move_leaves_none();
    test_pairs();
    test_self_move_keeps();
    test_emptied_right_names_no_identity();
    return 0;
}
