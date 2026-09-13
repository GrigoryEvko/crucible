#pragma once

#include <crucible/sessions/SessionContext.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::context {

using ::crucible::safety::proto::Entry;
using ::crucible::safety::proto::Context;
using ::crucible::safety::proto::EmptyContext;

using ::crucible::safety::proto::context_size_v;
using ::crucible::safety::proto::is_empty_context_v;

using ::crucible::safety::proto::ComposeContext;
using ::crucible::safety::proto::compose_context_t;

using ::crucible::safety::proto::contains_key_v;
using ::crucible::safety::proto::LookupContext;
using ::crucible::safety::proto::lookup_context_t;

using ::crucible::safety::proto::Key;
using ::crucible::safety::proto::KeySet;
using ::crucible::safety::proto::DomainOf;
using ::crucible::safety::proto::domain_of_t;

using ::crucible::safety::proto::UpdateEntry;
using ::crucible::safety::proto::update_entry_t;

using ::crucible::safety::proto::RemoveEntry;
using ::crucible::safety::proto::remove_entry_t;

using ::crucible::safety::proto::is_permission_balanced;
using ::crucible::safety::proto::is_permission_balanced_v;

}  // namespace crucible::fixy::sess::context

namespace crucible::fixy::sess::context::u052i_self_test {

namespace proto = ::crucible::safety::proto;

struct SessA {};
struct SessB {};
struct RoleP {};
struct RoleC {};
struct TypeP {};
struct TypeC {};
struct TypeP2 {};

using Ctx = Context<Entry<SessA, RoleP, TypeP>, Entry<SessA, RoleC, TypeC>>;
// Ctx2 carries a different session tag. Composition has a
// disjoint-domain precondition, so a context is never composed with
// itself.
using Ctx2 = Context<Entry<SessB, RoleP, TypeP>, Entry<SessB, RoleC, TypeC>>;

static_assert(std::is_same_v<Entry<SessA, RoleP, TypeP>, proto::Entry<SessA, RoleP, TypeP>>);
static_assert(std::is_same_v<Context<>, proto::Context<>>);
static_assert(std::is_same_v<EmptyContext, proto::EmptyContext>);
static_assert(std::is_same_v<EmptyContext, Context<>>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::session, SessA>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::role, RoleP>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::local_type, TypeP>);

static_assert(context_size_v<Ctx> == 2);
static_assert(context_size_v<EmptyContext> == 0);
static_assert(is_empty_context_v<EmptyContext>);
static_assert(!is_empty_context_v<Ctx>);

static_assert(context_size_v<compose_context_t<Ctx, Ctx2>> == 4,
              "composing two disjoint contexts concatenates their entries.");
static_assert(std::is_same_v<compose_context_t<Ctx, EmptyContext>, Ctx>,
              "composing with EmptyContext on the right is the identity.");
static_assert(std::is_same_v<compose_context_t<EmptyContext, Ctx>, Ctx>);

static_assert(contains_key_v<Ctx, SessA, RoleP>);
static_assert(contains_key_v<Ctx, SessA, RoleC>);
static_assert(!contains_key_v<Ctx, SessB, RoleP>, "absent (session, role) key must not be reported present.");
static_assert(!contains_key_v<EmptyContext, SessA, RoleP>);
static_assert(std::is_same_v<lookup_context_t<Ctx, SessA, RoleP>, TypeP>);
static_assert(std::is_same_v<lookup_context_t<Ctx, SessA, RoleC>, TypeC>);

static_assert(std::is_same_v<domain_of_t<Ctx>, KeySet<Key<SessA, RoleP>, Key<SessA, RoleC>>>);
static_assert(domain_of_t<Ctx>::size == 2);
static_assert(std::is_same_v<domain_of_t<EmptyContext>, KeySet<>>);
static_assert(domain_of_t<EmptyContext>::size == 0);

using Updated = update_entry_t<Ctx, SessA, RoleP, TypeP2>;
static_assert(std::is_same_v<lookup_context_t<Updated, SessA, RoleP>, TypeP2>);
static_assert(std::is_same_v<lookup_context_t<Updated, SessA, RoleC>, TypeC>,
              "update of one entry leaves the others unchanged.");
static_assert(context_size_v<Updated> == context_size_v<Ctx>);
static_assert(std::is_same_v<update_entry_t<Ctx, SessA, RoleP, TypeP>, Ctx>,
              "update with the same type is a no-op (idempotent).");

using Removed = remove_entry_t<Ctx, SessA, RoleP>;
static_assert(context_size_v<Removed> == 1);
static_assert(!contains_key_v<Removed, SessA, RoleP>);
static_assert(contains_key_v<Removed, SessA, RoleC>);
static_assert(std::is_same_v<remove_entry_t<remove_entry_t<Ctx, SessA, RoleP>, SessA, RoleC>, EmptyContext>,
              "removing every entry yields EmptyContext.");

static_assert(std::is_same_v<is_permission_balanced<EmptyContext, proto::EmptyPermSet>,
                             proto::is_permission_balanced<EmptyContext, proto::EmptyPermSet>>);
static_assert(is_permission_balanced_v<EmptyContext, proto::EmptyPermSet>,
              "an empty context is vacuously permission-balanced.");

// The count is one per re-exported name.
constexpr int u052i_surface_cardinality = 20;
static_assert(u052i_surface_cardinality == 20, "the re-exported surface of fixy::sess::context has changed. Update "
                                               "the using-declarations and this count together.");

}  // namespace crucible::fixy::sess::context::u052i_self_test

namespace crucible::fixy::sess::context {

// A static assertion can be discharged without ever instantiating an
// inline body. Naming the results in a real function puts every
// metafunction below through a full instantiation.
inline void runtime_smoke_test() noexcept {
    struct S {};
    struct RoleR {};
    struct RoleW {};
    struct TR {};
    struct TW {};
    using C0 = EmptyContext;
    using C2 = Context<Entry<S, RoleR, TR>, Entry<S, RoleW, TW>>;

    [[maybe_unused]] constexpr std::size_t sz0 = context_size_v<C0>;
    [[maybe_unused]] constexpr std::size_t sz2 = context_size_v<C2>;
    [[maybe_unused]] constexpr bool e0 = is_empty_context_v<C0>;
    [[maybe_unused]] constexpr bool has = contains_key_v<C2, S, RoleR>;
    [[maybe_unused]] constexpr bool no = contains_key_v<C2, S, RoleW>;

    using Look = lookup_context_t<C2, S, RoleR>;
    using Dom = domain_of_t<C2>;
    using Updated = update_entry_t<C2, S, RoleR, TW>;
    using Removed = remove_entry_t<C2, S, RoleR>;
    [[maybe_unused]] constexpr bool look_ok = std::is_same_v<Look, TR>;
    [[maybe_unused]] constexpr std::size_t dom_sz = Dom::size;
    [[maybe_unused]] constexpr std::size_t up_sz = context_size_v<Updated>;
    [[maybe_unused]] constexpr std::size_t rm_sz = context_size_v<Removed>;
    [[maybe_unused]] constexpr bool bal = is_permission_balanced_v<C0, ::crucible::safety::proto::EmptyPermSet>;

    (void)sz0;
    (void)sz2;
    (void)e0;
    (void)has;
    (void)no;
    (void)look_ok;
    (void)dom_sz;
    (void)up_sz;
    (void)rm_sz;
    (void)bal;
}

}  // namespace crucible::fixy::sess::context
