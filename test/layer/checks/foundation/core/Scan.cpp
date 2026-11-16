// The compile-time checks of foundation/core/Scan.h.

#include <foundation/core/Scan.h>

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace foundation::core {

namespace detail::scan_checks {

// A combine that is neither associative nor commutative.
struct Mix {
    [[nodiscard]] constexpr std::uint64_t operator()(std::uint64_t const& left,
                                                     std::uint64_t const& right) const noexcept {
        return rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }
};

inline constexpr std::uint64_t empty_value = 0xE3E3E3E3u;

// The depths of THE ERROR BOUND.
static_assert(exclusive_scan_depth(0) == 0 && exclusive_scan_depth(1) == 0 && exclusive_scan_depth(2) == 1);
static_assert(exclusive_scan_depth(3) == 2 && exclusive_scan_depth(4) == 2 && exclusive_scan_depth(7) == 4);
static_assert(inclusive_scan_depth(0) == 0 && inclusive_scan_depth(1) == 1 && inclusive_scan_depth(3) == 3);
static_assert(inclusive_scan_depth(7) == 5 && inclusive_scan_depth(8) == 4);

template <std::size_t N>
[[nodiscard]] consteval FixedArray<std::uint64_t, N> leaves_of() noexcept {
    FixedArray<std::uint64_t, N> leaves{};
    std::uint64_t next = 1;
    for (std::uint64_t& leaf : leaves) {
        leaf = next++;
    }
    return leaves;
}

// Runs write on the ViewMut of the count first elements of owner, through a
// frame of the owner and a split of its ViewMut, and gives the bool of write.
template <class Owner, class Write>
[[nodiscard]] consteval bool write_first(Owner& owner, std::size_t count, Write write) noexcept {
    bool is_done = false;
    owner = with_mut(std::move(owner), [&](auto whole) noexcept {
        auto [part, rest] = split_at(std::move(whole), count).expect("the part fits the owner");
        static_cast<void>(rest.size());
        is_done = write(std::move(part));
    });
    return is_done;
}

// Writes the plan of kind into the count first steps of steps and into
// outputs, through a frame of each.
template <class Steps, class Outputs>
[[nodiscard]] consteval bool write_plan_into(ScanKind kind, Steps& steps, std::size_t count,
                                             Outputs& outputs) noexcept {
    bool is_written = false;
    steps = with_mut(std::move(steps), [&](auto step_out) noexcept {
        auto [part, rest] = split_at(std::move(step_out), count).expect("the steps fit");
        static_cast<void>(rest.size());
        outputs = with_mut(std::move(outputs), [&](auto output_out) noexcept {
            is_written = write_scan_plan(kind, std::move(part), std::move(output_out)).is_ok();
        });
    });
    return is_written;
}

// The outputs of a plan into out, with the count first slots of scratch,
// through a frame of each.
template <class Scratch, class Out>
[[nodiscard]] consteval bool evaluate_plan_into(View<TreeStep const> steps, View<PlanNode const> outputs,
                                                View<std::uint64_t const> leaves, Scratch& scratch, std::size_t count,
                                                Out& out) noexcept {
    bool is_done = false;
    scratch = with_mut(std::move(scratch), [&](auto scratch_out) noexcept {
        auto [part, rest] = split_at(std::move(scratch_out), count).expect("the scratch fits");
        static_cast<void>(rest.size());
        out = with_mut(std::move(out), [&](auto out_part) noexcept {
            is_done =
                evaluate_scan_plan(steps, outputs, leaves, std::move(part), std::move(out_part), empty_value, Mix{})
                    .is_ok();
        });
    });
    return is_done;
}

// The plan of an exclusive scan of four: out[0] is empty, out[1] is x0, out[2]
// is the pair (x0, x1), and out[3] is that pair with x2.
[[nodiscard]] consteval bool exclusive_plan_of_four_is_stated() noexcept {
    FixedArray<TreeStep, 2> steps{};
    FixedArray<PlanNode, 4> outputs{};
    bool const is_written = write_plan_into(ScanKind::Exclusive, steps, 2, outputs);
    return is_written && steps.at(index<0>()) == TreeStep{PlanNode{0}, PlanNode{1}}
        && steps.at(index<1>()) == TreeStep{PlanNode{4}, PlanNode{2}} && outputs.at(index<0>()) == empty_plan_node
        && outputs.at(index<1>()) == PlanNode{0} && outputs.at(index<2>()) == PlanNode{4}
        && outputs.at(index<3>()) == PlanNode{5};
}
static_assert(scan_plan_steps(4, ScanKind::Exclusive).value_or(0) == 2);
static_assert(exclusive_plan_of_four_is_stated());

// The plan of an inclusive scan of four: x0, (x0, x1), ((x0, x1), x2), and
// that with x3.
[[nodiscard]] consteval bool inclusive_plan_of_four_is_stated() noexcept {
    FixedArray<TreeStep, 3> steps{};
    FixedArray<PlanNode, 4> outputs{};
    bool const is_written = write_plan_into(ScanKind::Inclusive, steps, 3, outputs);
    return is_written && steps.at(index<0>()) == TreeStep{PlanNode{0}, PlanNode{1}}
        && steps.at(index<1>()) == TreeStep{PlanNode{4}, PlanNode{2}}
        && steps.at(index<2>()) == TreeStep{PlanNode{5}, PlanNode{3}} && outputs.at(index<0>()) == PlanNode{0}
        && outputs.at(index<1>()) == PlanNode{4} && outputs.at(index<2>()) == PlanNode{5}
        && outputs.at(index<3>()) == PlanNode{6};
}
static_assert(scan_plan_steps(4, ScanKind::Inclusive).value_or(0) == 3);
static_assert(inclusive_plan_of_four_is_stated());

// The plan of a scan of N leaves gives the outputs of the scan for the
// combine Mix.  Each step feeds a later step or an output, and no two steps
// are the same.
template <std::size_t N, ScanKind Kind>
[[nodiscard]] consteval bool plan_agrees_with_scan() noexcept {
    constexpr std::size_t step_count = scan_plan_steps(N, Kind).value_or(0);
    auto const leaves = leaves_of<N>();
    FixedArray<TreeStep, step_count + 1> step_storage{};
    FixedArray<std::uint64_t, step_count + 1> scratch_storage{};
    FixedArray<PlanNode, N> outputs{};
    if (!write_plan_into(Kind, step_storage, step_count, outputs)) return false;
    View<TreeStep const> const steps =
        View<TreeStep const>{step_storage.view()}.window(0, step_count).expect("the steps fit");
    FixedArray<std::uint64_t, N> from_plan{};
    FixedArray<std::uint64_t, N> from_scan{};
    View<std::uint64_t const> const leaf_view{leaves.view()};
    if (!evaluate_plan_into(steps, View<PlanNode const>{outputs.view()}, leaf_view, scratch_storage, step_count,
                            from_plan)) {
        return false;
    }
    bool const is_scanned = write_first(from_scan, N, [&](auto out) noexcept {
        if constexpr (Kind == ScanKind::Inclusive) {
            return inclusive_scan(leaf_view, std::move(out), Mix{}).is_ok();
        } else {
            return exclusive_scan(leaf_view, std::move(out), empty_value, Mix{}).is_ok();
        }
    });
    if (!is_scanned || !equal(from_plan.view(), from_scan.view())) return false;
    // Each step has a reader: a later step or an output.
    FixedArray<bool, N + step_count> is_read{};
    View<bool> const read_marks = is_read.view();
    for (TreeStep const step : steps) {
        *read_marks.get(static_cast<std::size_t>(step.left)).expect("an operand is a node of the plan") = true;
        *read_marks.get(static_cast<std::size_t>(step.right)).expect("an operand is a node of the plan") = true;
    }
    for (PlanNode const output : outputs) {
        if (output != empty_plan_node) {
            *read_marks.get(static_cast<std::size_t>(output)).expect("an output is a node of the plan") = true;
        }
    }
    for (std::size_t index = N; index < N + step_count; ++index) {
        if (!*read_marks.get(index).expect("the node is inside the plan")) return false;
    }
    // No step repeats another.  The check is quadratic, so it runs on the
    // smaller plans.
    if constexpr (N <= 20) {
        for (std::size_t index = 0; index < step_count; ++index) {
            for (std::size_t later = index + 1; later < step_count; ++later) {
                if (*steps.get(later).expect("the step is inside the plan")
                    == *steps.get(index).expect("the step is inside the plan")) {
                    return false;
                }
            }
        }
    }
    return true;
}

template <std::size_t... N>
[[nodiscard]] consteval bool plans_agree(std::index_sequence<N...> /*counts*/) noexcept {
    return ((plan_agrees_with_scan<N + 1, ScanKind::Inclusive>() && plan_agrees_with_scan<N + 1, ScanKind::Exclusive>())
            && ...);
}

// The runtime test of the scans proves the plans of larger counts, so the
// check of each count here stops at 20.
static_assert(plans_agree(std::make_index_sequence<20>{}));
static_assert(plan_agrees_with_scan<63, ScanKind::Inclusive>() && plan_agrees_with_scan<64, ScanKind::Exclusive>());
static_assert(plan_agrees_with_scan<65, ScanKind::Inclusive>() && plan_agrees_with_scan<65, ScanKind::Exclusive>());

// out[k] depends only on the elements before it: the scan of six gives the
// first five outputs of the scan of five.
[[nodiscard]] consteval bool scan_is_stable_under_append() noexcept {
    auto const leaves = leaves_of<6>();
    FixedArray<std::uint64_t, 6> longer{};
    FixedArray<std::uint64_t, 5> shorter{};
    View<std::uint64_t const> const all{leaves.view()};
    bool const is_scanned =
        write_first(longer, 6,
                    [&](auto out) noexcept { return exclusive_scan(all, std::move(out), empty_value, Mix{}).is_ok(); })
        && write_first(shorter, 5, [&](auto out) noexcept {
               return exclusive_scan(all.window(0, 5).expect("five leaves fit"), std::move(out), empty_value, Mix{})
                   .is_ok();
           });
    return is_scanned
        && equal(View<std::uint64_t const>{longer.view()}.window(0, 5).expect("five outputs fit"), shorter.view());
}
static_assert(scan_is_stable_under_append());

// A scan into a View of another count writes nothing.
[[nodiscard]] consteval bool mismatched_scan_is_refused() noexcept {
    auto const leaves = leaves_of<4>();
    FixedArray<std::uint64_t, 3> out{};
    return write_first(out, 3, [&](auto part) noexcept {
        return inclusive_scan(View<std::uint64_t const>{leaves.view()}, std::move(part), Mix{}).is_err();
    });
}
static_assert(mismatched_scan_is_refused());

}  // namespace detail::scan_checks

}  // namespace foundation::core
