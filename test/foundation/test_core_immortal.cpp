// The holder of foundation/core/Immortal.h.
//
//   - A fill builds the payload in the storage, and get() reads it through a
//     Ref, also from a const holder.
//   - The end of a holder runs no destructor of its payload.
//   - A second fill and a read before the fill each end the child process
//     with the text of the fatal exit.

#include <foundation/core/Immortal.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

namespace core = ::foundation::core;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

// The count of the destructors of Counted that ran.
constinit std::uint32_t ended = 0;

// A payload whose destructor counts its runs.
struct Counted final {
    std::uint64_t word = 0;

    explicit Counted(std::uint64_t value) noexcept : word{value} {}
    Counted(Counted const&) = delete("a payload of the test lives in its holder");
    Counted& operator=(Counted const&) = delete("a payload of the test lives in its holder");
    ~Counted() { ++ended; }
};

void fill_then_read(TestRunnerCtx const& test) {
    core::Immortal<Counted> holder{};
    CRUCIBLE_TEST_ASSERT(test, !holder.is_filled());
    holder.fill(std::uint64_t{0x1234});
    CRUCIBLE_TEST_ASSERT(test, holder.is_filled());
    CRUCIBLE_TEST_ASSERT(test, holder.get()->word == 0x1234);
    holder.get()->word = 0x5678;
    core::Immortal<Counted> const& reader = holder;
    CRUCIBLE_TEST_ASSERT(test, reader.get()->word == 0x5678);
    pass(test, "  fill_then_read: PASSED\n");
}

void end_runs_no_destructor(TestRunnerCtx const& test) {
    std::uint32_t const before = ended;
    {
        core::Immortal<Counted> holder{};
        holder.fill(std::uint64_t{7});
    }
    CRUCIBLE_TEST_ASSERT(test, ended == before);
    pass(test, "  end_runs_no_destructor: PASSED\n");
}

void second_fill_dies(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            core::Immortal<Counted> holder{};
            holder.fill(std::uint64_t{1});
            holder.fill(std::uint64_t{2});
        },
        core::TextView{"a second fill would build another one over it"});
    pass(test, "  second_fill_dies: PASSED\n");
}

void read_before_fill_dies(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            core::Immortal<Counted> holder{};
            static_cast<void>(holder.get()->word);
        },
        core::TextView{"a read of an Immortal came before its fill"});
    pass(test, "  read_before_fill_dies: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    fill_then_read(test);
    end_runs_no_destructor(test);
    second_fill_dies(test);
    read_before_fill_dies(test);
    pass(test, "test_core_immortal: PASSED\n");
    return 0;
}
