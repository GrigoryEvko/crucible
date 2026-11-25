// Tests of the code of an element that reaches the owner of the element: the
// end or the move of an element reads the owner, or it changes the owner.
//
// An owner holds a live element in each slot that it counts while such code
// runs, so a read finds no ended element.
//
// Each death runs in a child process through expect_dies of the Check family,
// which requires SIGABRT and the text of the refusal.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Brand.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

[[nodiscard]] ::foundation::effects::Alloc allocation() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The reads of an owner by the code of an element, and what they found.
constinit std::uint32_t reads_of_an_owner = 0;
constinit bool is_ended_element_seen = false;

// Reads each element of an owner.
template <class Element>
void read_each(core::View<Element const> view) noexcept {
    ++reads_of_an_owner;
    for (Element const& element : view)
        is_ended_element_seen = is_ended_element_seen || !element.is_live();
}

// Tests that the code of the elements read their owner count times and found
// live elements only, and starts the next case.
void check_whole_reads(std::uint32_t count) {
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_an_owner == count && !is_ended_element_seen);
    reads_of_an_owner = 0;
}

// ── The end of an owner ────────────────────────────────────────────────────

// The holder of an owner of each kind.  An element of the owner reaches the
// holder through a Ref.
struct ArrayTag final {};

template <class Tag>
struct Holder;
template <class Tag>
struct HolderBrand final {};
template <class Tag>
using HolderRef = core::Ref<Holder<Tag>, HolderBrand<Tag>>;

// An element whose end reads the owner that its holder holds.
template <class Tag>
class Ending final {
public:
    explicit Ending(HolderRef<Tag> holder) noexcept : holder_{holder} {}
    Ending(Ending&&) noexcept = default;
    Ending(Ending const&) = delete("an element of the test has one identity");
    Ending& operator=(Ending const&) = delete("an element of the test has one identity");
    ~Ending() {
        is_live_ = false;
        holder_->read();
    }

    [[nodiscard]] bool is_live() const noexcept { return is_live_; }

private:
    HolderRef<Tag> holder_;
    bool is_live_ = true;
};

template <>
struct Holder<ArrayTag> final {
    using brand_type = HolderBrand<ArrayTag>;
    core::HeapArray<Ending<ArrayTag>> held{};

    void read() const noexcept { read_each(held.view()); }
};

// The end of an owner empties it before it ends its elements, so the end of each
// element finds an empty owner.
void test_the_end_of_an_owner_empties_it_first() {
    {
        Holder<ArrayTag> holder{};
        holder.held = core::mint_heap_array_with<Ending<ArrayTag>>(
            allocation(), 3, [&holder](std::size_t) noexcept { return Ending<ArrayTag>{core::to(holder)}; });
    }
    check_whole_reads(3);
    ::fixy::check::pass(kTest, "  test_the_end_of_an_owner_empties_it_first: PASSED\n");
}

}  // namespace

int main() {
    test_the_end_of_an_owner_empties_it_first();
    ::fixy::check::pass(kTest, "test_core_element_code: all tests passed\n");
    return 0;
}
