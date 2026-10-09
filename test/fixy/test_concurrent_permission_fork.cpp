// Tests of the fork of fixy/concurrent/PermissionFork.h, which starts one
// thread for each child.  The bodies run at the same time, the fork builds
// the copy of each body on the calling thread in child order, the parent
// comes back with its brand, and a fork can run inside the door of a read
// view.  The tsan preset runs the same binary.

#include <fixy/concurrent/PermissionFork.h>

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/ReadView.h>

#include <type_traits>
#include <utility>

namespace {

namespace fork_tags {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
struct Config {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace fork_tags

}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<fork_tags::Whole, fork_tags::Left, fork_tags::Right> : std::true_type {};
}  // namespace foundation::permissions

namespace {

namespace eff = ::foundation::effects;
using ::foundation::core::Atomic;
using ::foundation::permissions::mint_permission_root;
using ::foundation::permissions::permission_drop;
using ::foundation::permissions::WriteView;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

constinit thread_local bool is_main_thread = false;

// Two bodies that wait on each other end only if they run at the same
// time, so the fork starts a thread for each child.  The parent comes back
// with the brand that it had before the split.
void test_bodies_run_at_the_same_time() {
    Atomic<bool> left_arrived{false};
    Atomic<bool> right_arrived{false};
    Atomic<bool> left_on_main{true};
    Atomic<bool> right_on_main{true};

    auto whole = mint_permission_root<fork_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    auto rebuilt = ::fixy::concurrent::mint_permission_fork<fork_tags::Left, fork_tags::Right>(
        BgCtx{eff::testing::bg()}, std::move(whole),
        [&](WriteView<fork_tags::Left, WholeBrand> const&, BgCtx const&) noexcept {
            left_on_main.store_release(is_main_thread);
            left_arrived.store_release(true);
            while (!right_arrived.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
        },
        [&](WriteView<fork_tags::Right, WholeBrand> const&, BgCtx const&) noexcept {
            right_on_main.store_release(is_main_thread);
            right_arrived.store_release(true);
            while (!left_arrived.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
        });
    static_assert(std::is_same_v<decltype(rebuilt), decltype(whole)>, "the parent comes back with its own brand");
    CRUCIBLE_FATAL_INVARIANT(!left_on_main.load_acquire() && !right_on_main.load_acquire());
    permission_drop(std::move(rebuilt));
}

// The copies of the recorded bodies, and the runs that found each fact of
// their copy correct.
constinit Atomic<int> body_copies{0};
constinit Atomic<int> correct_runs{0};

// A body that records where the fork copies it.  The fork copies a body
// that the caller gives by name, and the copy then moves to the thread of
// its child.
struct RecordedBody {
    int copy_order = 0;
    bool was_copied_on_main = false;

    RecordedBody() noexcept = default;
    RecordedBody(RecordedBody const&) noexcept
        : copy_order{body_copies.fetch_add_acq_rel(1) + 1}, was_copied_on_main{is_main_thread} {}
    RecordedBody(RecordedBody&&) noexcept = default;
    RecordedBody& operator=(RecordedBody const&) = delete("the test copies a body only through the fork");
    RecordedBody& operator=(RecordedBody&&) = delete("the test copies a body only through the fork");
    ~RecordedBody() = default;

    template <typename View>
    void operator()(View const&, BgCtx const&) noexcept {
        // Order 1 is the copy of the left body and order 2 is the copy of
        // the right body, and each copy took place on the main thread.
        int const expected = std::is_same_v<typename View::tag_type, fork_tags::Left> ? 1 : 2;
        if (copy_order == expected && was_copied_on_main && !is_main_thread) {
            static_cast<void>(correct_runs.fetch_add_acq_rel(1));
        }
    }
};

void test_bodies_are_copied_on_the_calling_thread_in_child_order() {
    RecordedBody const left{};
    RecordedBody const right{};
    auto rebuilt = ::fixy::concurrent::mint_permission_fork<fork_tags::Left, fork_tags::Right>(
        BgCtx{eff::testing::bg()}, mint_permission_root<fork_tags::Whole>(), left, right);
    CRUCIBLE_FATAL_INVARIANT(body_copies.load_acquire() == 2);
    CRUCIBLE_FATAL_INVARIANT(correct_runs.load_acquire() == 2);
    permission_drop(std::move(rebuilt));
}

// Two writers, each with its own write permission, read one view of a
// shared configuration.  The fork joins before the body of the door
// returns, so each child holds a reference to a view that outlives it.
void test_fork_inside_the_door_of_a_read_view() {
    Atomic<int> left_done{0};
    Atomic<int> right_done{0};

    auto whole = mint_permission_root<fork_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;

    auto config_back = ::foundation::permissions::with_read_view(
        mint_permission_root<fork_tags::Config>(), [&](auto const& config_view) noexcept {
            auto rebuilt = ::fixy::concurrent::mint_permission_fork<fork_tags::Left, fork_tags::Right>(
                BgCtx{eff::testing::bg()}, std::move(whole),
                [&config_view, &left_done](WriteView<fork_tags::Left, WholeBrand> const&, BgCtx const&) noexcept {
                    static_cast<void>(config_view);
                    left_done.store_release(1);
                },
                [&config_view, &right_done](WriteView<fork_tags::Right, WholeBrand> const&, BgCtx const&) noexcept {
                    static_cast<void>(config_view);
                    right_done.store_release(1);
                });
            // The fork has returned, so both workers have joined.
            CRUCIBLE_FATAL_INVARIANT(left_done.load_acquire() == 1);
            CRUCIBLE_FATAL_INVARIANT(right_done.load_acquire() == 1);
            permission_drop(std::move(rebuilt));
        });
    permission_drop(std::move(config_back));
}

}  // namespace

int main() {
    is_main_thread = true;
    test_bodies_run_at_the_same_time();
    test_bodies_are_copied_on_the_calling_thread_in_child_order();
    test_fork_inside_the_door_of_a_read_view();
    return 0;
}
