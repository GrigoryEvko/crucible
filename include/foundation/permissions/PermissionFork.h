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
// copy and no move, and only the write borrow door constructs one, so
// nothing that a body does with its view yields a token.  The fork
// rebuilds the parent with mint_permission_combine_n, which consumes the
// children that the split gave, so the rebuild is the combine of a split
// and nothing else.
//
// This header starts no thread.  It holds the view, the write borrow door
// with_write_view, the checks of a fork, and the arm that runs the bodies
// one after another on the calling thread.  The arm that runs each body on
// a thread of its own is mint_permission_fork of
// fixy/concurrent/PermissionFork.h, above the thread door of fixy/os.  The
// layer above chooses between the two arms, because this layer carries no
// cost model.
//
// A callable's own noexcept specification is all that is checked here.
// The fixy::atom::ctrl::throws atom is above this layer.
// fixy/concurrent/Spawn.h refuses a callable whose type carries it.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::permissions {

namespace detail {

// The only code that constructs a WriteView.  It has no member that
// returns a view: a view exists only as a local of lend, for the time that
// the body runs.
struct write_view_door;

}  // namespace detail

// The proof that a body holds: exclusive access to one region for the
// time that the body runs.  It carries the brand of the token that the
// door lends, so it names that region and no other region of the tag.
//
// No copy and no move, so no view leaves the body that the door lends it
// to, and only the door constructs one.  With every copy and move deleted
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
                                                       "the brand of the token that the door lends.");

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
    // A proof that is built from nothing is not a proof.  Only the door
    // reaches this constructor.
    constexpr WriteView() noexcept {}

    friend struct detail::write_view_door;
};

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionForkInline =
    ::foundation::effects::IsExecCtx<Ctx> && CtxAdmitsPermission<Parent, Ctx>
    && (CtxAdmitsPermission<Children, Ctx> && ...)
    && can_split_into_pack_v<Parent, Children...> && has_split_pack_authoring_witness_v<Parent, Children...>;

namespace detail {

// The body of one borrow, and of one child of a fork.  Each arm of a fork
// keeps one copy of each body and calls that copy as an lvalue, so the
// check calls the decayed type as an lvalue too: the body that the check
// admits is the body that runs.
template <typename Callable, typename Child, typename Brand, typename Ctx>
concept PermissionForkBody =
    std::is_invocable_v<std::decay_t<Callable>&, WriteView<Child, Brand> const&, Ctx const&>
    && std::is_nothrow_invocable_v<std::decay_t<Callable>&, WriteView<Child, Brand> const&, Ctx const&>;

// True when the two tuples are std::tuple, and each body borrows the view
// of its own child, of the parent's brand, with the context, without
// throwing.  A pack of bodies whose length is not the length of the
// children takes nothing.  The test is a function that is not a template,
// so no translation unit can specialize it to start a body that cannot
// take its child.
[[nodiscard]] consteval bool each_body_takes_its_child(std::meta::info ctx, std::meta::info brand,
                                                       std::meta::info children_tuple,
                                                       std::meta::info callables_tuple) {
    const std::meta::info children_type = std::meta::dealias(children_tuple);
    const std::meta::info callables_type = std::meta::dealias(callables_tuple);
    const bool are_tuples = std::meta::has_template_arguments(children_type)
                         && std::meta::has_template_arguments(callables_type)
                         && std::meta::template_of(children_type) == (^^std::tuple)
                         && std::meta::template_of(callables_type) == (^^std::tuple);
    if (!are_tuples) return false;
    const std::vector<std::meta::info> children = std::meta::template_arguments_of(children_type);
    const std::vector<std::meta::info> callables = std::meta::template_arguments_of(callables_type);
    if (children.size() != callables.size()) return false;
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (!std::meta::extract<bool>(
                std::meta::substitute(^^PermissionForkBody, {callables[index], children[index], brand, ctx}))) {
            return false;
        }
    }
    return true;
}

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

// The check that each arm makes after its constraint.  The constraint of
// each mint refuses a count mismatch and a body that cannot take its child
// first.  The split that a fork delegates to asserts the pairwise-distinct
// check again, and asserting it here makes the diagnostic name the arm.
template <bool Spawn, typename... Children>
consteval void permission_fork_check_() noexcept {
    static_assert(tags_are_distinct({^^Children...}),
                  fork_diagnostic(Spawn, Spawn ? ": Child region tags must be PAIRWISE DISTINCT — "
                                                 "forking two threads with Permission<A> each would alias "
                                                 "region A and produce a data race."
                                               : ": Child region tags must be PAIRWISE DISTINCT — "
                                                 "two bodies with Permission<A> each would alias region A."));
}

struct write_view_door {
    // Complexity: the cost of the body.  The frame holds the token and one
    // empty view, so the door adds no instruction when it is inlined.  The
    // door discards the result of the body, as a fork does.
    template <typename Ctx, typename Tag, typename Brand, typename Body>
    [[nodiscard]] static constexpr Permission<Tag, Brand> lend(Ctx const& ctx, Permission<Tag, Brand>&& token,
                                                               Body& body) noexcept {
        Permission<Tag, Brand> held{std::move(token)};
        WriteView<Tag, Brand> const view{};
        static_cast<void>(body(view, ctx));
        return held;
    }
};

}  // namespace detail

// The write borrow.  The door takes the token by value category rvalue,
// keeps it in its own frame, gives the body a WriteView by const reference
// and the context, and hands the token back when the body returns.  While
// the body runs, the caller holds no name for the token, so the body
// cannot move it, end it or split it.  The context admits the row of the
// tag, as each mint of the tag asks.
template <typename Ctx, typename Tag, typename Brand, typename Body>
    requires ::foundation::effects::IsExecCtx<Ctx> && CtxAdmitsPermission<Tag, Ctx>
          && detail::PermissionForkBody<Body, Tag, Brand, Ctx>
[[nodiscard]] constexpr Permission<Tag, Brand> with_write_view(Ctx const& ctx, Permission<Tag, Brand>&& token,
                                                               Body&& body) noexcept {
    return detail::write_view_door::lend(ctx, std::move(token), body);
}

// A named token stays with the caller while the body runs, so the body
// could move it or end it through a capture.  The deleted twin refuses a
// named token and a const one, which cannot move into the frame.  It takes
// no token that the caller gives with std::move, so a call that the gate of
// the door refuses names that gate.
template <typename Ctx, typename Token, typename Body>
    requires IsPermission<std::remove_cvref_t<Token>>
              && (std::is_lvalue_reference_v<Token> || std::is_const_v<std::remove_reference_t<Token>>)
void with_write_view(Ctx const&, Token&&, Body&&) =
    delete("with_write_view lends its token to the body and hands the token back.  Pass the token with std::move, "
           "and keep the token that the call returns");

// The bodies run one after another on the calling thread, in child order.
// Each body borrows the view of its own child through with_write_view,
// and the fork combines the children that the door gave back.
//
// The clause names two gates, and folding them behind one name to read as
// a single concept was measured and reverted.  The children half then
// goes through a variable template over a tuple of the child tags, which
// is one atomic constraint, so the compiler stops reporting there: the
// fixtures on this mint lost the conjunct that answered false, the pack
// that does not partition, and named a tuple wrapper instead.  Both
// halves stay spelled here.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && (detail::each_body_takes_its_child(^^Ctx, ^^Brand, ^^std::tuple<Children...>, ^^std::tuple<Callables...>))
[[nodiscard]] constexpr Permission<Parent, Brand>
mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept {
    detail::permission_fork_check_<false, Children...>();
    auto children = mint_permission_split_n<Children...>(ctx, std::move(parent));
    auto bodies = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};
    // The comma fold runs the bodies in child order.  Each door gives its
    // child back into the place that it took the child from.
    [&]<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept {
        ((std::get<Is>(children) = with_write_view(ctx, std::move(std::get<Is>(children)), std::get<Is>(bodies))), ...);
    }(std::index_sequence_for<Children...>{});
    // The children never left this frame.  The combine consumes each of
    // them, and it checks the manifest and the brand again.
    return std::apply(
        [&ctx](auto&... child) noexcept { return mint_permission_combine_n<Parent>(ctx, std::move(child)...); },
        children);
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
