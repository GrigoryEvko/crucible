#pragma once

// Encodes the parallel composition rule of concurrent separation logic:
//
//   {P1} C1 {Q1}   ...   {Pn} Cn {Qn}
//   ─────────────────────────────────────────────────
//   {P1 * ... * Pn} (C1 || ... || Cn) {Q1 * ... * Qn}
//
// The parent permission splits into disjoint children, one per body.
// The children stay in the frame of the fork.  Each body borrows its own
// child through a WriteView, and once every body has finished, the fork
// combines the children that it kept into the parent.  Disjointness is
// settled when the split manifest is checked, so no two bodies can reach
// the same region.
//
// A body never holds a child token.  A body that held one could move it
// out of the fork, and the parent that the fork gives back would then
// exist beside a live child: two exclusive tokens over one region.  A
// body that returns its token does not close this route.  A token is
// empty, so a moved-from token is a valid token of the same type, and a
// body can return one after it moved the real one away.  The view has no
// copy and no move, and only the fork constructs one, so nothing that a
// body does with its view yields a token.  The fork rebuilds the parent
// with mint_permission_combine_n, which consumes the children that the
// split gave, so the rebuild is the combine of a split and nothing else.
//
// This layer carries no cost model, so it cannot decide whether the
// bodies run on their own threads or inline in child order.  It offers
// the two arms as two mints and the layer above chooses.  The spawning
// arm requires a context that owns the background effect; the inline
// arm requires no effect beyond what the tags themselves need.
//
// The two arms share one body and one set of checks; the arm is a bool
// parameter of both, and the checks' diagnostics name the arm they
// fire for.  The body is a private member of PermissionForkRunner, whose
// only friends are the two mints.
//
// One thread per child, spawned and joined inside the call.  That suits
// a few children with long bodies.  It is the wrong shape for many short
// tasks, which want a work-stealing pool instead.
//
// A callable's own noexcept specification is all that is checked here.
// The fixy::atom::ctrl::throws atom is above this layer.  fixy/os/Spawn.h
// refuses a callable whose type carries it.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::permissions {

class PermissionForkRunner;

// The proof that a fork body holds: exclusive access to one child region
// for the time that the body runs.  It carries the brand of the parent,
// so it names the region of that fork and no other region of the tag.
//
// No copy and no move, so no view leaves the body that the fork lends it
// to, and only the fork constructs one.  With every copy and move deleted
// and the default constructor private, no constructor is trivial.  The
// destructor is user-provided because GCC 16 counts a deleted member as
// trivial and would call the class trivially copyable, so std::bit_cast
// would build a view from a byte.  A destructor that is not trivial
// refuses std::bit_cast and std::start_lifetime_as.  It compiles to
// nothing, because the class is empty.
//
// What stays open, as for ReadView: a body can store a pointer to its
// view in an object outside its frame.  The pointer dangles when the body
// returns.  It proves nothing that a door reads, because no door takes a
// view by pointer and no door turns a view into a token.
template <typename Tag, typename Brand>
class [[nodiscard]] WriteView {
    static_assert(PermissionTag<Tag>, "WriteView<Tag, Brand>: Tag must be an empty non-union class type, as a "
                                      "Permission tag is.");
    static_assert(::foundation::brand::IsBrand<Brand>, "WriteView<Tag, Brand>: Brand must be an empty class type: "
                                                       "the brand of the parent of the fork.");

public:
    using tag_type = Tag;
    using brand_type = Brand;

    WriteView(const WriteView&) = delete("a WriteView lives in the frame of its body. A copy could outlive the join");
    WriteView(WriteView&&) = delete("a WriteView lives in the frame of its body. A move could carry it out");
    WriteView& operator=(const WriteView&) = delete("a WriteView binds one body. Assignment would rebind it");
    WriteView& operator=(WriteView&&) = delete("a WriteView binds one body. Assignment would rebind it");

    constexpr ~WriteView() {}

    static void* operator new(std::size_t) = delete("a WriteView lives in the frame of its body, not on the heap");
    static void* operator new[](std::size_t) = delete("a WriteView lives in the frame of its body, not on the heap");
    static void* operator new(std::size_t,
                              std::align_val_t) = delete("a WriteView lives in the frame of its body, not on the heap");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("a WriteView lives in the frame of its body, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

private:
    // A proof that is built from nothing is not a proof.  Only the fork
    // reaches this constructor.
    constexpr WriteView() noexcept {}

    friend class PermissionForkRunner;
};

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionForkInline =
    ::foundation::effects::IsExecCtx<Ctx> && CtxAdmitsPermission<Parent, Ctx>
    && (CtxAdmitsPermission<Children, Ctx> && ...)
    && can_split_into_pack_v<Parent, Children...> && has_split_pack_authoring_witness_v<Parent, Children...>;

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork = CtxFitsPermissionForkInline<Ctx, Parent, Children...>
                             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

namespace detail {

// Each arm keeps one copy of each body and calls that copy as an lvalue,
// so the check calls the decayed type as an lvalue too: the body that
// the check admits is the body that runs.
template <typename Callable, typename Child, typename Brand, typename Ctx>
concept PermissionForkBody =
    std::is_invocable_v<std::decay_t<Callable>&, WriteView<Child, Brand> const&, Ctx const&>
    && std::is_nothrow_invocable_v<std::decay_t<Callable>&, WriteView<Child, Brand> const&, Ctx const&>;

// Each body borrows the view of its own child, of the parent's brand,
// with the context, without throwing.  A pack of bodies whose length is
// not the length of the children, or a shape that is not two tuples,
// takes nothing.
template <typename Ctx, typename Brand, typename ChildrenTuple, typename CallablesTuple>
struct can_each_body_take_its_child : std::false_type {};

template <typename Ctx, typename Brand, typename... Children, typename... Callables>
    requires(sizeof...(Children) == sizeof...(Callables))
struct can_each_body_take_its_child<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<(PermissionForkBody<Callables, Children, Brand, Ctx> && ...)> {};

template <typename Ctx, typename Brand, typename ChildrenTuple, typename CallablesTuple>
inline constexpr bool can_each_body_take_its_child_v =
    can_each_body_take_its_child<Ctx, Brand, ChildrenTuple, CallablesTuple>::value;

// A static_assert message assembled at compile time, so one check can
// name the arm it fires for.  The buffer is sized for the longest
// message below; the text past the terminator is never read.
struct fork_diagnostic {
    std::array<char, 256> text{};
    std::size_t length = 0;

    consteval fork_diagnostic(bool spawn, std::string_view rest) noexcept {
        append(spawn ? "mint_permission_fork" : "mint_permission_fork_inline");
        append(rest);
    }
    consteval void append(std::string_view part) noexcept {
        for (char c : part) {
            if (length < text.size()) text[length++] = c;
        }
    }
    [[nodiscard]] consteval std::size_t size() const noexcept { return length; }
    [[nodiscard]] consteval char const* data() const noexcept { return text.data(); }
};

// The four checks both arms make.  The split this delegates to asserts
// the pairwise-distinct one again; asserting it here makes the
// diagnostic name the fork.  The constraint on the runner's body refuses
// a count mismatch and a body that cannot take its child first, so of
// the four only the pairwise-distinct check can fire.
template <bool Spawn, typename Ctx, typename Brand, typename ChildrenTuple, typename CallablesTuple>
consteval void permission_fork_check_() noexcept {
    []<typename... Children, typename... Callables>(std::tuple<Children...>*, std::tuple<Callables...>*) {
        static_assert(sizeof...(Children) == sizeof...(Callables),
                      fork_diagnostic(Spawn, ": number of Child tags must match number of callables."));
        static_assert(all_distinct_tags_v<Children...>,
                      fork_diagnostic(Spawn, Spawn ? ": Child region tags must be PAIRWISE DISTINCT — "
                                                     "forking two threads with Permission<A> each would alias "
                                                     "region A and produce a data race."
                                                   : ": Child region tags must be PAIRWISE DISTINCT — "
                                                     "two bodies with Permission<A> each would alias region A."));
        static_assert(
            (std::is_invocable_v<std::decay_t<Callables>&, WriteView<Children, Brand> const&, Ctx const&> && ...),
            fork_diagnostic(Spawn, ": each callable must be invocable as "
                                   "Callable_i(WriteView<Child_i, Brand> const&, Ctx const&)."));
        static_assert(
            (std::is_nothrow_invocable_v<std::decay_t<Callables>&, WriteView<Children, Brand> const&, Ctx const&>
             && ...),
            fork_diagnostic(Spawn, ": callables must be noexcept."));
    }(static_cast<ChildrenTuple*>(nullptr), static_cast<CallablesTuple*>(nullptr));
}

// The fit of one arm: the spawning arm needs the background effect, the
// inline arm does not.  The runner below states it again on its own body,
// so the check stands where the threads start and not only at the doors.
template <bool Spawn, typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionForkArm = (Spawn && CtxFitsPermissionFork<Ctx, Parent, Children...>)
                                || (!Spawn && CtxFitsPermissionForkInline<Ctx, Parent, Children...>);

}  // namespace detail

// The two mints, declared before the runner so that its friend
// declarations name them and nothing else.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                             Callables&&... callables) noexcept;

template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent, Brand>
mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept;

// The body of a fork: split the parent, lend each body the view of its
// child, and combine the children once every body has finished.  It does
// the work of the two mints, so only the two mints may reach it.  Every
// member is private and static, the two mints are the only friends, and
// the class is final and cannot be built, so no other scope can call a
// member, derive to reach one, or take one's address.
//
// A free function template in a detail namespace once held this body.
// Any translation unit could call it, and it carried no constraint, so a
// caller under the foreground context started threads the spawning mint
// would have refused for want of the background effect.
//
// The friend declarations repeat each mint's constraint exactly, because
// the language requires it.  The class sits in this namespace, not in
// detail, for the reason Permission.h gives for perm_mint_key: a friend
// declaration in a nested namespace would declare a new function there
// and befriend that one instead.
class PermissionForkRunner final {
    // No object of the runner exists.  Every constructor is deleted and
    // the destructor is user-provided, so the class is neither trivially
    // copyable nor an implicit-lifetime type, and no byte route
    // (std::bit_cast, std::start_lifetime_as) can make one either.
    PermissionForkRunner() = delete("the fork runner holds static members only; no object of it exists");
    PermissionForkRunner(const PermissionForkRunner&) = delete("the fork runner holds static members only");
    PermissionForkRunner& operator=(const PermissionForkRunner&) = delete("the fork runner holds static members only");
    PermissionForkRunner(PermissionForkRunner&&) = delete("the fork runner holds static members only");
    PermissionForkRunner& operator=(PermissionForkRunner&&) = delete("the fork runner holds static members only");
    constexpr ~PermissionForkRunner() noexcept {}

    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires CtxFitsPermissionFork<Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
    friend Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                          Callables&&... callables) noexcept;

    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
    friend constexpr Permission<Parent, Brand>
    mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept;

    // Runs one body with the view of its child.  The view is a local of
    // this frame, so it ends when the body returns.
    template <typename Child, typename Brand, typename Ctx, typename Body>
    static constexpr void run_body_(Body& body, Ctx const& ctx) noexcept {
        WriteView<Child, Brand> const view{};
        body(view, ctx);
    }

    template <typename Brand, typename... Children, typename Ctx, typename Bodies, std::size_t... Is>
    static void spawn_(Ctx const& ctx, Bodies& bodies, std::index_sequence<Is...>) noexcept {
        // A jthread constructor is not noexcept, because the thread creation
        // under it can fail on resource exhaustion.  Without the catch, that
        // failure reaches this function's noexcept boundary and terminates
        // when the build has exceptions on, but aborts when it does not.  The
        // catch makes both builds abort, which is what a resource failure does
        // everywhere else here.
#if defined(__cpp_exceptions)
        try {
#endif
            [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {std::jthread{
                [body = std::move(std::get<Is>(bodies)), child_ctx = ctx](std::stop_token) mutable noexcept {
                    run_body_<Children...[Is], Brand>(body, child_ctx);
                }}...};
#if defined(__cpp_exceptions)
        } catch (...) {
            // The catch is deliberately untyped.  The contract is that any
            // failure to construct a thread aborts, and the exception type a
            // given standard library reports it with is not fixed.
            std::abort();
        }
#endif
    }

    template <typename Brand, typename... Children, typename Ctx, typename Bodies, std::size_t... Is>
    static constexpr void run_inline_(Ctx const& ctx, Bodies& bodies, std::index_sequence<Is...>) noexcept {
        (run_body_<Children...[Is], Brand>(std::get<Is>(bodies), ctx), ...);
    }

    // The children carry the parent's brand, the views carry it into the
    // bodies, and the combine carries it back out, so the region that
    // returns is the one that went in.
    template <bool Spawn, typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires detail::CtxFitsPermissionForkArm<Spawn, Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
    static constexpr Permission<Parent, Brand> run_(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                    Callables&&... callables) noexcept {
        detail::permission_fork_check_<Spawn, Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>();

        auto children = mint_permission_split_n<Children...>(ctx, std::move(parent));

        auto bodies = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

        if constexpr (Spawn) {
            spawn_<Brand, Children...>(ctx, bodies, std::index_sequence_for<Children...>{});
        } else {
            run_inline_<Brand, Children...>(ctx, bodies, std::index_sequence_for<Children...>{});
        }

        // The children never left this frame.  The combine consumes each
        // of them, and it checks the manifest and the brand again.
        return std::apply(
            [&ctx](auto&... child) noexcept { return mint_permission_combine_n<Parent>(ctx, std::move(child)...); },
            children);
    }
};

// The clause names two gates, and folding them behind one name to read as
// a single concept was measured and reverted.  The children half then
// goes through a variable template over a tuple of the child tags, which
// is one atomic constraint, so the compiler stops reporting there: the
// two fixtures on this mint lost the conjunct that answered false — the
// absent background capability, the pack that does not partition — and
// named a tuple wrapper instead.  Both halves stay spelled here.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                             Callables&&... callables) noexcept {
    return PermissionForkRunner::run_<true, Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

// The bodies run one after another on the calling thread, in child
// order.  The split and the combine are the same as the spawning arm's,
// so a body still borrows its own child and nothing else.
// The inline arm spells its two halves for the same reason as the arm
// above, and differs from it in one conjunct: it demands no background
// capability, because it starts no thread.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, Brand, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent, Brand>
mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept {
    return PermissionForkRunner::run_<false, Children...>(ctx, std::move(parent),
                                                          std::forward<Callables>(callables)...);
}

namespace row_discipline {
struct write_view;
}  // namespace row_discipline

}  // namespace foundation::permissions

// A view over a region folds the region's row as its payload, as the
// tokens and the read view do, so a view over an IO region is not a view
// over a pure one.
namespace foundation::diag {

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::WriteView<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::write_view,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

}  // namespace foundation::diag
