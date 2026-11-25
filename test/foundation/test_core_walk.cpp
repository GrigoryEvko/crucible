// Tests of the Walk family of foundation/core/Walk.h: the adapters and the
// terminals of a walk over a View.  Each property compares a chain with a
// loop over the proven indexes of the same array, which is the model.

#include <foundation/core/Array.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::ElementCount;
using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::LengthMismatch;
using ::foundation::core::View;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void fill_with_stream(Run& run, ::crucible::test::PhiloxStream& stream) {
    for (auto const index : run.indices()) {
        run.set(index, stream.next_wide() % 1000);
    }
}

[[nodiscard]] constexpr bool is_kept(std::uint64_t const& value) noexcept { return value % 3 != 0; }

// A float fold adds in the left order, so the bits are the bits of the plain
// loop.  The order (1e8 + 1) - 1e8 + 1 gives 1, and a different order gives 2.
void test_float_fold_runs_left_to_right() {
    FixedArray<float, 4> const values{1.0e8f, 1.0f, -1.0e8f, 1.0f};
    float const folded =
        values.view().walk().fold(0.0f, [](float sum, float const& value) noexcept { return sum + value; });
    float plain = 0.0f;
    for (float const value : values) {
        plain += value;
    }
    using ::foundation::core::bit_cast;
    CRUCIBLE_TEST_ASSERT(kTest, bit_cast<std::uint32_t>(folded) == bit_cast<std::uint32_t>(plain));
    CRUCIBLE_TEST_ASSERT(kTest, bit_cast<std::uint32_t>(folded) == bit_cast<std::uint32_t>(1.0f));
}

// The writer of a frame writes the elements that the walks of the View then
// read.  get_mut writes each element of a window, and write_each writes each
// element from the element of a source at the same position.
void test_writer_writes_what_the_walk_reads() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 5>,
                                                                     FixedArray<int, 5>{1, 2, 3, 4, 5});
    values.with_mut(values_key, [](auto run) noexcept {
        auto middle = ::foundation::core::take_window(std::move(run), 1, 3).expect("three elements fit");
        for (std::size_t position = 0; position < middle.size(); ++position) {
            auto value = middle.get_mut(position).expect("the position is inside the window");
            *value *= 10;
        }
    });
    FixedArray<int, 5> const expected{1, 20, 30, 40, 5};
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::equal(values.view(), expected.view()));
    int const middle_sum = values.view()
                               .walk()
                               .skip(ElementCount{1})
                               .limit(ElementCount{3})
                               .fold(0, [](int sum, int const& value) noexcept { return sum + value; });
    CRUCIBLE_TEST_ASSERT(kTest, middle_sum == 90);
    FixedArray<int, 3> const source{7, 8, 9};
    auto const [is_written, target] = ::foundation::core::build_fixed_array<int, 3>([&source](auto run) noexcept {
        return ::foundation::core::write_each(
                   std::move(run), [](int const origin) noexcept { return origin + 1; }, source.view())
            .is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_written);
    CRUCIBLE_TEST_ASSERT(kTest, target == (FixedArray<int, 3>{8, 9, 10}));
}

void test_try_zip_compares_the_counts() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    View<int const> const head = values.view().window(0, 3).expect("three elements fit");
    View<int const> const tail = values.view().window(3, 3).expect("three elements fit");
    View<int const> const longer = values.view().window(1, 4).expect("four elements fit");
    CRUCIBLE_TEST_ASSERT(kTest, head.walk().try_zip(longer.walk()).is_err());
    bool const is_mismatch =
        head.walk()
            .try_zip(longer.walk())
            .match([](auto) noexcept { return false; }, [](LengthMismatch) noexcept { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, is_mismatch);
    auto const pairs = head.walk().try_zip(tail.walk()).expect("the two windows hold three elements");
    CRUCIBLE_TEST_ASSERT(kTest, pairs.size() == 3);
    int total = 0;
    for (auto const [first, second] : pairs) {
        CRUCIBLE_TEST_ASSERT(kTest, *second == *first + 3);
        total += *first;
    }
    CRUCIBLE_TEST_ASSERT(kTest, total == 1 + 2 + 3);
    View<int const> const empty{};
    CRUCIBLE_TEST_ASSERT(kTest,
                         empty.walk().try_zip(empty.walk()).expect("two empty runs hold zero elements").size() == 0);
}

// The property: for random runs and counts, each chain gives the value of the
// loop over the indexes of the model.
void test_chains_agree_with_the_index_model() {
    ::crucible::test::PhiloxStream stream{0x57A1C0DE0000E804u};
    Run run;
    Run other;
    for (int trial = 0; trial < 1024; ++trial) {
        fill_with_stream(run, stream);
        fill_with_stream(other, stream);
        std::size_t const size = stream.below(run_size + 1);
        std::size_t const limit = stream.below(run_size + 2);
        std::size_t const skipped = stream.below(run_size + 2);
        std::uint64_t const wanted = stream.below(1000);
        View<std::uint64_t const> const elements = run.view().window(0, size).expect("the size is at most the run");
        View<std::uint64_t const> const others = other.view().window(0, size).expect("the size is at most the run");

        std::uint64_t model_squares = 0;
        std::size_t model_kept = 0;
        std::uint64_t model_kept_prefix = 0;
        std::size_t kept_so_far = 0;
        std::uint64_t model_skipped = 0;
        std::uint64_t model_dot = 0;
        bool is_wanted_found = false;
        bool is_each_below = true;
        for (std::size_t position = 0; position < size; ++position) {
            std::uint64_t const value = element_at(run, position);
            model_squares = model_squares * 31 + value * value;
            if (is_kept(value)) {
                ++model_kept;
                if (kept_so_far < limit) {
                    model_kept_prefix = model_kept_prefix * 7 + value;
                    ++kept_so_far;
                }
            }
            if (position >= skipped) model_skipped += value;
            model_dot += value * element_at(other, position);
            is_wanted_found = is_wanted_found || value == wanted;
            is_each_below = is_each_below && value < wanted;
        }

        CRUCIBLE_TEST_ASSERT(
            kTest, elements.walk()
                           .map([](std::uint64_t const& value) noexcept { return value * value; })
                           .fold(std::uint64_t{0},
                                 [](std::uint64_t acc, std::uint64_t square) noexcept { return acc * 31 + square; })
                       == model_squares);
        CRUCIBLE_TEST_ASSERT(kTest, elements.walk().filter(is_kept).count() == model_kept);
        CRUCIBLE_TEST_ASSERT(
            kTest, elements.walk()
                           .filter(is_kept)
                           .limit(ElementCount{limit})
                           .fold(std::uint64_t{0},
                                 [](std::uint64_t acc, std::uint64_t const& value) noexcept { return acc * 7 + value; })
                       == model_kept_prefix);
        CRUCIBLE_TEST_ASSERT(
            kTest, elements.walk()
                           .skip(ElementCount{skipped})
                           .fold(std::uint64_t{0},
                                 [](std::uint64_t acc, std::uint64_t const& value) noexcept { return acc + value; })
                       == model_skipped);
        CRUCIBLE_TEST_ASSERT(kTest, elements.walk().limit(ElementCount{limit}).size() == (limit < size ? limit : size));
        CRUCIBLE_TEST_ASSERT(
            kTest, elements.walk()
                           .try_zip(others.walk())
                           .expect("the two windows hold size elements")
                           .map([](auto const& pair) noexcept {
                               auto const& [first, second] = pair;
                               return *first * *second;
                           })
                           .fold(std::uint64_t{0},
                                 [](std::uint64_t acc, std::uint64_t product) noexcept { return acc + product; })
                       == model_dot);
        CRUCIBLE_TEST_ASSERT(kTest, elements.walk().any_of([wanted](std::uint64_t const& value) noexcept {
            return value == wanted;
        }) == is_wanted_found);
        CRUCIBLE_TEST_ASSERT(kTest, elements.walk().all_of([wanted](std::uint64_t const& value) noexcept {
            return value < wanted;
        }) == is_each_below);

        std::size_t cursor_kept = 0;
        for (std::uint64_t const value : elements.walk().filter(is_kept)) {
            CRUCIBLE_TEST_ASSERT(kTest, is_kept(value));
            ++cursor_kept;
        }
        CRUCIBLE_TEST_ASSERT(kTest, cursor_kept == model_kept);

        // flat_map: each element gives the run of its first value % 8
        // elements of the other run.
        std::size_t model_flat = 0;
        for (std::size_t position = 0; position < size; ++position) {
            model_flat += element_at(run, position) % 8;
        }
        View<std::uint64_t const> const pool = other.view();
        CRUCIBLE_TEST_ASSERT(kTest, elements.walk()
                                            .flat_map([pool](std::uint64_t const& value) noexcept {
                                                return pool.window(0, value % 8).expect("a prefix fits").walk();
                                            })
                                            .count()
                                        == model_flat);
    }
}

// The brand of the nodes of a chain of the tests.
struct ChainNodeBrand final {};

struct ChainNode;

// The Ref of a node of a chain of the tests, with the brand of the nodes.
using NodeRef = ::foundation::core::Ref<ChainNode const, ChainNodeBrand>;
using NodeLink = ::foundation::core::Option<NodeRef>;

// A node of a chain of the tests: a value and the link to the next node.
struct ChainNode {
    using brand_type = ChainNodeBrand;

    int value = 0;
    NodeLink next{::foundation::core::none};
};

// The step of a chain of the tests: the next node of a node.
struct NextChainNode {
    [[nodiscard]] constexpr NodeLink operator()(NodeRef const& node) const noexcept { return node->next; }
};

// The link to a node of a chain of the tests.
[[nodiscard]] NodeLink link_to(ChainNode const& node) noexcept { return NodeLink::some(::foundation::core::to(node)); }

// A walk of a chain gives each node from the first link to the first node
// with no next node, in the order of the links, and an empty first link gives
// no node.  The terminals stop where the walk of the chain stops.
void test_chain_walk_follows_each_link() {
    using ::foundation::core::walk_chain;
    ChainNode const fourth{.value = 4, .next = ::foundation::core::none};
    ChainNode const third{.value = 3, .next = link_to(fourth)};
    ChainNode const second{.value = 2, .next = link_to(third)};
    ChainNode const first{.value = 1, .next = link_to(second)};

    int const total = walk_chain(link_to(first), NextChainNode{}).fold(0, [](int sum, NodeRef node) noexcept {
        return sum * 10 + node->value;
    });
    CRUCIBLE_TEST_ASSERT(kTest, total == 1234);
    CRUCIBLE_TEST_ASSERT(kTest, walk_chain(link_to(first), NextChainNode{}).count() == 4);
    CRUCIBLE_TEST_ASSERT(kTest, walk_chain(link_to(third), NextChainNode{}).count() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, walk_chain(NodeLink{::foundation::core::none}, NextChainNode{}).count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, walk_chain(link_to(first), NextChainNode{}).any_of([](NodeRef const& node) noexcept {
        return node->value == 3;
    }));
    CRUCIBLE_TEST_ASSERT(kTest, !walk_chain(link_to(first), NextChainNode{}).any_of([](NodeRef const& node) noexcept {
        return node->value == 5;
    }));
    CRUCIBLE_TEST_ASSERT(kTest, walk_chain(link_to(first), NextChainNode{}).all_of([](NodeRef const& node) noexcept {
        return node->value > 0;
    }));

    // The cursor loop reads the same nodes, and a break leaves the chain.
    auto const chain = walk_chain(link_to(first), NextChainNode{});
    int read = 0;
    for (NodeRef const node : chain) {
        if (node->value == 3) break;
        read = read * 10 + node->value;
    }
    CRUCIBLE_TEST_ASSERT(kTest, read == 12);
    CRUCIBLE_TEST_ASSERT(kTest, chain.begin() == chain.begin() && !(chain.begin() == chain.end()));
    auto const empty = walk_chain(NodeLink{::foundation::core::none}, NextChainNode{});
    CRUCIBLE_TEST_ASSERT(kTest, empty.begin() == empty.end());
}

// A cursor of a walk never leaves its loop: a read at the end and a step
// past the end end the process in each build.  Each one runs in a child
// through expect_dies of the Check family.
void test_cursors_at_the_end_abort(::fixy::TestRunnerCtx const& test) {
    using ::fixy::check::expect_dies;
    FixedArray<int, 4> const values{1, 2, 3, 4};
    expect_dies(
        test,
        [&values] noexcept {
            auto const pairs = values.view().walk().zip(values.view().walk());
            static_cast<void>(*pairs.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a zip walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const pairs = values.view().walk().zip(values.view().walk());
            auto cursor = pairs.end();
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a zip walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const kept =
                values.view().walk().filter([](int const&) noexcept { return true; }).limit(ElementCount{2});
            static_cast<void>(*kept.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a limit of a walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const kept =
                values.view().walk().filter([](int const&) noexcept { return true; }).limit(ElementCount{2});
            auto cursor = kept.begin();
            ++cursor;
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a limit of a walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const doubled = values.view().walk().map([](int const& value) noexcept { return value * 2; });
            static_cast<void>(*doubled.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a View"});
    expect_dies(
        test,
        [] noexcept {
            auto const chain = ::foundation::core::walk_chain(NodeLink{::foundation::core::none}, NextChainNode{});
            static_cast<void>(*chain.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a chain walk"});
    expect_dies(
        test,
        [] noexcept {
            auto const chain = ::foundation::core::walk_chain(NodeLink{::foundation::core::none}, NextChainNode{});
            auto cursor = chain.end();
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a chain walk"});
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_float_fold_runs_left_to_right();
    test_writer_writes_what_the_walk_reads();
    test_try_zip_compares_the_counts();
    test_chains_agree_with_the_index_model();
    test_chain_walk_follows_each_link();
    test_cursors_at_the_end_abort(test);
    ::fixy::check::pass(kTest, "test_core_walk: all tests passed\n");
    return 0;
}
