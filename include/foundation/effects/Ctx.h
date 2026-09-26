#pragma once

// An execution context is the scope an operation runs in: which
// capability source the caller holds, and which effects the scope has
// claimed.  A body that claims row R may run in a context whose row
// covers R, and a context may claim no more than its capability source
// permits.  That pair is the whole of reject-by-default for effects: a
// context minted with the empty row admits nothing effectful, and every
// relaxation is a wider row named in the type.
//
// The old context (include/crucible/effects/_ExecCtx.h) carried six
// further axes: NUMA policy, allocator class, heat tier, cache
// residency, workload hint, progress class.  Outside the old substrate
// no production site read them; the three that spelled the tags
// (topology/TopologyGraph.h, ledger/LedgerStore.h, mimic/CogMimic.h)
// were naming a context, not reading an axis.  Inside the old substrate
// they had readers, and those readers are what the later port tasks
// decide about: concurrent/ExecCtxBridge.h and SubstrateCtxFit.h
// project the residency and workload axes into the cache-tier gates on
// the channel and session mints, WorkloadBudgetCoherent.h and
// AutoSplit.h read the workload hint, Endpoint.h reads the heat tier
// and the allocator class, permissions/PermissionFork.h reads the
// workload budget to choose between running the children inline and
// spawning them, and sessions/_SessionMint.h re-exports every axis.
// This layer carries none of that policy.  Where a caller wants to say
// how hot a path is or which allocator it reaches for, it grades the
// value with the band wrapper for that axis; a channel or a fork that
// wants a budget takes it as its own parameter.  A context describes
// the surrounding scope, not a value.

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <meta>
#include <string_view>
#include <thread>
#include <type_traits>

namespace foundation::effects {

namespace testing {
struct ForegroundWitness;
}  // namespace testing

// The owner of the foreground key, and the producer claim that is its one
// friend.  Both are defined below in this header, for the reason
// Effect.h gives for the other owners.
namespace host {
struct ForegroundOwner;
template <class Brand>
class ProducerClaim;
}  // namespace host

// Declared here so that a branded source can name the one context that
// builds it in place.  The definition and its default arguments are below.
template <class Cap, class Row>
class ExecCtx;

namespace detail::ctx_mint {

// Both constructors are user-provided, for the reason Effect.h gives for
// the other keys: a key with a trivial constructor is built by
// std::bit_cast or std::start_lifetime_as without the access check.
class fg_key {
private:
    constexpr fg_key() noexcept {}

    friend struct ::foundation::effects::host::ForegroundOwner;
    friend struct ::foundation::effects::testing::ForegroundWitness;

public:
    constexpr fg_key(const fg_key&) noexcept {}
};

}  // namespace detail::ctx_mint

namespace ctx_cap {
// The foreground thread holds no value atom.  A reach for one fails to
// compile, because this source has no member of any capability type.  It
// permits the empty row, spelled the way a context spells its own, so
// that one reader below serves every source.
//
// It is still evidence.  A branded source names the single-producer
// state whose claim built it, and only the claim of that brand builds
// one.  A gate of the brand therefore states that its caller runs on the
// thread that owns that state, and it refuses the claim of any other
// state.  The unbranded source below names no state, and any translation
// unit can declare a brand of its own, so no route turns a branded source
// into an unbranded one.  Only the test door builds the unbranded source.
//
// A branded source has no copy and no move, so the context that holds one
// has none either.  A by-value lambda capture or a thread argument makes
// a copy, and the copy on the other thread is evidence of a claim that
// the thread does not hold.  The context builds the source in place from
// the key, and every gate takes the context by reference.  The destructor
// is user-provided, so the type is not trivially copyable and
// std::bit_cast refuses it.  The annotation makes the checked lifetime
// start refuse it and every class that holds one.
//
// A reference to a context can still reach another thread, for example
// through a lambda that captures it by reference.  A cold gate of the
// brand therefore also checks the calling thread at run time, against the
// live claims of the brand (host::require_brand_thread below).  A gate
// that runs for each op does not do this check, because the check reads
// the thread identity.  One residual stays: a reference to a context,
// captured and used on another thread, at a gate that runs for each op.
// The run-time check also admits every thread while no claim of the brand
// is live.  The test door builds a context with no claim, and a context
// can stay after the claim that built it.
//
// The unbranded source keeps a trivial copy constructor, so a call passes
// it in no register.  Its copy assignment is user-provided, so
// std::bit_cast refuses it, and the annotation refuses the checked
// lifetime start.
template <class Brand>
class [[=::foundation::lifetime::no_start_over_bytes{}]] BrandedFg {
public:
    using brand_type = Brand;

    template <template <Effect...> class R>
    using permitted_as = R<>;

    BrandedFg(const BrandedFg&) = delete("a copy of a branded source can go to a thread that holds no claim");
    BrandedFg(BrandedFg&&) = delete("a moved branded source can go to a thread that holds no claim");
    BrandedFg& operator=(const BrandedFg&) = delete("a copy of a branded source can go to a thread that holds no claim");
    BrandedFg& operator=(BrandedFg&&) = delete("a moved branded source can go to a thread that holds no claim");

    // User-provided, so the source is not trivially copyable.  GCC counts a
    // class whose copies are all deleted as trivially copyable, and
    // std::bit_cast would then build one from a byte.
    constexpr ~BrandedFg() noexcept {}

private:
    constexpr explicit BrandedFg(detail::ctx_mint::fg_key) noexcept {}

    // Only the context builds the source, from the key that a producer
    // claim or the test witness gives it.
    friend class ::foundation::effects::ExecCtx<BrandedFg, ::foundation::effects::Row<>>;
};

class [[=::foundation::lifetime::no_start_over_bytes{}]] Fg {
public:
    template <template <Effect...> class R>
    using permitted_as = R<>;

    constexpr explicit Fg(detail::ctx_mint::fg_key) noexcept {}

    constexpr Fg(const Fg&) noexcept = default;
    constexpr Fg(Fg&&) noexcept = default;
    constexpr Fg& operator=(const Fg&) noexcept { return *this; }
    constexpr Fg& operator=(Fg&&) noexcept { return *this; }
    ~Fg() = default;
};

// These name the same three types the enclosing namespace declares.
// Either spelling works; the aliases only give the axis a uniform look.
using Bg = ::foundation::effects::Bg;
using Init = ::foundation::effects::Init;
using Test = ::foundation::effects::Test;
}  // namespace ctx_cap

// A capability source is the foreground marker or a rostered context.
// This is a concept over the roster in Effect.h, so nothing a
// translation unit declares can add a source.  The value and the trait
// spellings are derived from it and read by nothing that gates.
//
// A branded source is recognized by reflection on its template, not by a
// trait a translation unit could specialize.
namespace detail {
template <class T>
[[nodiscard]] consteval bool is_branded_foreground_() noexcept {
    return std::meta::has_template_arguments(^^T) && std::meta::template_of(^^T) == ^^ctx_cap::BrandedFg;
}
}  // namespace detail

template <class T>
concept IsBrandedForeground = detail::is_branded_foreground_<T>();

template <class T>
concept IsCapType = std::same_as<T, ctx_cap::Fg> || IsBrandedForeground<T> || IsContext<T>;
template <class T>
inline constexpr bool is_cap_type_v = IsCapType<T>;
template <class T>
struct is_cap_type : std::bool_constant<IsCapType<T>> {};

// The row recognition trait, is_effect_row, lives in Row.h beside the
// row it recognizes.

// The largest row each capability source can authorize, read off the
// source's own declaration: a context permits its own atom and the
// value atoms it holds, and the foreground marker permits nothing.  A
// context's own row must stay inside it, which is what stops a
// foreground context from claiming a background effect.
template <IsCapType Cap>
struct cap_permitted_row {
    using type = typename Cap::template permitted_as<Row>;
};

template <IsCapType Cap>
using cap_permitted_row_t = typename cap_permitted_row<Cap>::type;

// The two checks are folded into one concept so that a bad argument
// reports one diagnostic.  Conjunction short-circuits at the first
// failing atom, which also stops the Subrow atom from being substituted
// and asking a non-capability type for its permitted row.
//
// The concept is checked by a static_assert in the class body rather
// than by a constraint on the template head.  The friend declaration
// that names this template sits in Effect.h, which cannot include the
// row algebra without a cycle, so it could not repeat a matching
// constraint.
template <class Cap, class Row>
concept WellFormedExecCtx = IsCapType<Cap> && IsEffectRow<Row> && Subrow<Row, cap_permitted_row_t<Cap>>;

template <class Cap = ctx_cap::Fg, class Row = ::foundation::effects::Row<>>
class [[nodiscard]] ExecCtx {
public:
    static_assert(WellFormedExecCtx<Cap, Row>,
                  "One argument to ExecCtx is not a member of its axis, or the row exceeds what the "
                  "capability source permits.  The axes are a capability source (ctx_cap::Fg, Bg, Init, "
                  "Test) and an effect Row that is a subrow of what the source permits.");

private:
    // The members are private, and the capability member is why.  It
    // holds a context whose own default constructor is private and
    // friended here, so exposing the member would let any translation
    // unit copy out a capability context that it could not have
    // constructed for itself.
    //
    // Making this a class rather than a struct changes only the default
    // member access.
    [[no_unique_address]] Cap cap_{};
    [[no_unique_address]] Row row_{};

public:
    // Every context is handed the capability it claims, and that
    // capability IS the evidence.  No capability source has a public
    // default constructor: a background, init or test source comes from
    // mint_context, and a foreground source from its key.
    //
    // There is no default constructor.  One existed for the foreground
    // context, on the reasoning that a context which claims nothing has
    // nothing to forge.  A gate that admits only the foreground context
    // claims something, though: that the caller runs on the thread that
    // won the producer claim, and a default constructor let any thread
    // make that claim.  An older form of it was public for every
    // specialization, and `ExecCtx<Init, Row<Init, Alloc, IO>>{}` then
    // built an init context without the init key.
    constexpr explicit ExecCtx(Cap cap) noexcept
        requires(!IsBrandedForeground<Cap>)
        : cap_{cap} {}

private:
    // A branded source has no copy, so the context builds it in place from
    // the key.  This constructor is private, so only a producer claim and
    // the test witness build a branded context.
    constexpr explicit ExecCtx(detail::ctx_mint::fg_key key) noexcept
        requires IsBrandedForeground<Cap>
        : cap_{key} {}

    template <class Brand>
    friend class host::ProducerClaim;
    friend struct testing::ForegroundWitness;

public:

    // The only way to reach the capability, and it borrows rather than
    // copies.  Code that wants a copy has to write one, which a grep
    // for this accessor finds.
    [[nodiscard]] constexpr Cap const& cap() const noexcept { return cap_; }

    using cap_type = Cap;
    using row_type = Row;

    // Each builder returns a fresh context with one axis replaced.
    // Every link of a chain is a distinct type and every link is one
    // byte.
    // Promoting the capability axis takes the capability itself, not
    // merely its name.  This used to be `with_cap<NewCap>()`, naming
    // the type and handing back a context that owned one, which is how
    // `ExecCtx<>{}.with_cap<Init>()` climbed from a foreground context
    // to an init context in a single call with no evidence at all.
    template <class NewCap>
        requires IsCapType<NewCap> && Subrow<Row, cap_permitted_row_t<NewCap>>
    [[nodiscard]] constexpr auto with_cap(NewCap cap) const noexcept -> ExecCtx<NewCap, Row> {
        return ExecCtx<NewCap, Row>{cap};
    }

    // Widening the row carries the capability already held rather than
    // minting one, which is why it needs no evidence beyond the context
    // it is called on.  The row may not grow past what the capability
    // source permits, so a foreground context — whose source permits
    // nothing — cannot widen at all.
    template <class NewRow>
        requires IsEffectRow<NewRow> && Subrow<Row, NewRow> && Subrow<NewRow, cap_permitted_row_t<Cap>>
    [[nodiscard]] constexpr auto in_row() const noexcept -> ExecCtx<Cap, NewRow> {
        return ExecCtx<Cap, NewRow>{cap_};
    }

    [[nodiscard]] static consteval std::string_view kind_name() noexcept { return "ExecCtx"; }
};

// The one door of the foreground context.  The key is the evidence: its
// constructor is private to the foreground owner and to the test witness.
[[nodiscard]] constexpr auto mint_foreground_context(detail::ctx_mint::fg_key key) noexcept
    -> ExecCtx<ctx_cap::Fg, Row<>> {
    return ExecCtx<ctx_cap::Fg, Row<>>{ctx_cap::Fg{key}};
}

namespace detail::producer_claim {

// The fields of the record of one brand (host::ProducerClaim below).  The
// type holds no key and builds no context.  Only a private function of the
// claim holds an object of it.
struct BrandRecord {
    std::atomic_flag lock{};
    std::atomic<const void*> holder{nullptr};
    std::size_t claims = 0;
};

}  // namespace detail::producer_claim

namespace host {

// The one production builder of the foreground key.  Its member is
// private, and the producer claims below are its only friends.
struct ForegroundOwner final {
private:
    [[nodiscard]] static constexpr auto key() noexcept -> detail::ctx_mint::fg_key { return detail::ctx_mint::fg_key{}; }

    template <class Brand>
    friend class ProducerClaim;
};

// The one production route to a foreground context.  The first thread
// that asks for a context holds the claim for the life of this object.
// A request from any other thread ends the process.
//
// Only the brand builds its claim, and the context names the brand.  The
// single-producer state S holds a ProducerClaim<S>, so a context branded
// S is evidence that its holder runs on the thread that owns an S.  A
// thread that holds the claim of another state passes no gate of S.
template <class Brand>
class ProducerClaim {
public:
    ProducerClaim(const ProducerClaim&) = delete("a claim names one owner of single-producer state");
    ProducerClaim& operator=(const ProducerClaim&) = delete("a claim names one owner of single-producer state");
    ProducerClaim(ProducerClaim&&) = delete("a claim names one owner of single-producer state");
    ProducerClaim& operator=(ProducerClaim&&) = delete("a claim names one owner of single-producer state");

    // User-provided, so the claim is neither trivially copyable nor an
    // implicit-lifetime type.  GCC counts a class whose copies are all
    // deleted as trivially copyable, and std::start_lifetime_as would
    // then build a claim over bytes that a thread already holds.  A claim
    // that a thread won also removes itself from the record of its brand.
    ~ProducerClaim() noexcept {
        if (holder_.load(std::memory_order_relaxed) != std::thread::id{}) leave_brand_();
    }

    // The part that inlines is a relaxed load, a comparison and a branch.
    // The claim and the failure report sit out of line and cold.  The body
    // reads the thread identity and an atomic, so it is not constexpr.
    [[nodiscard]] CRUCIBLE_INLINE auto mint_producer_context() noexcept  // MINT-PATTERN-OK: reads the thread id
        -> ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> {
        const auto current_tid = std::this_thread::get_id();
        if (holder_.load(std::memory_order_relaxed) != current_tid) [[unlikely]]
            claim_or_reject_(current_tid);
        return ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>>{ForegroundOwner::key()};
    }

    // True when no thread holds the claim yet, or the calling thread
    // holds it.  Two threads that both ask before either claims both read
    // true, and the loser of the claim then ends the process.
    [[nodiscard]] bool is_claimable_by_caller() const noexcept {
        const auto holder = holder_.load(std::memory_order_relaxed);
        return holder == std::thread::id{} || holder == std::this_thread::get_id();
    }

    // True when no claim of this brand is live, or the calling thread
    // holds the live claims.  A cold gate of the brand asks this through
    // require_brand_thread below.
    [[nodiscard]] static bool can_caller_use_brand() noexcept {
        const void* const holder = brand_record_().holder.load(std::memory_order_acquire);
        return holder == nullptr || holder == calling_thread_identity_();
    }

private:
    constexpr ProducerClaim() noexcept = default;

    friend Brand;

    [[gnu::cold, gnu::noinline]] void claim_or_reject_(std::thread::id current_tid) noexcept {
        auto holder = holder_.load(std::memory_order_relaxed);
        if (holder == std::thread::id{}) {
            // Relaxed is enough: the claim synchronizes nothing, it only
            // records which thread arrived first.  A failed exchange leaves
            // the winner's id in `holder`, which the check below reports.
            if (holder_.compare_exchange_strong(holder, current_tid, std::memory_order_relaxed)) {
                const bool is_only_brand_thread = enter_brand_(calling_thread_identity_());
                if (is_only_brand_thread) return;
                // Another thread holds a live claim of this brand.  The claim
                // is undone first, so that its destructor removes no entry
                // that it did not make.
                holder_.store(std::thread::id{}, std::memory_order_relaxed);
                CRUCIBLE_FATAL_INVARIANT(is_only_brand_thread);
            }
        }
        // Not a contract clause.  The predicate is about a thread identity
        // this function just read, and a contract evaluates to nothing in a
        // target built with the semantic set to `ignore`.
        CRUCIBLE_FATAL_INVARIANT(holder == current_tid);
    }

    // The record of a brand names the one thread that holds its live
    // claims, and counts them.  A second thread that wins a claim of the
    // brand while the first holds one is refused, because the record then
    // cannot tell the two threads apart.  Only a won claim and its
    // destructor write the record, so the flag that guards the count is
    // never on the path that runs for each op.
    [[gnu::cold, gnu::noinline]] static bool enter_brand_(const void* thread_identity) noexcept {
        BrandRecord& record = brand_record_();
        lock_brand_(record);
        const bool is_only_brand_thread =
            record.claims == 0 || record.holder.load(std::memory_order_relaxed) == thread_identity;
        if (is_only_brand_thread) {
            record.holder.store(thread_identity, std::memory_order_release);
            ++record.claims;
        }
        unlock_brand_(record);
        return is_only_brand_thread;
    }

    [[gnu::cold, gnu::noinline]] static void leave_brand_() noexcept {
        BrandRecord& record = brand_record_();
        lock_brand_(record);
        const bool has_entry = record.claims != 0;
        if (has_entry && --record.claims == 0) record.holder.store(nullptr, std::memory_order_release);
        unlock_brand_(record);
        CRUCIBLE_FATAL_INVARIANT(has_entry);
    }

    // The record names a thread by the address of a byte that each thread
    // owns, not by std::thread::id.  The default constructor of
    // std::thread::id is not constexpr, so an atomic of it cannot be
    // constinit, and the record needs constinit: it belongs to a template,
    // and the dynamic initialization of a templated static is unordered.  A
    // null address names no thread.  Like a std::thread::id, an address can
    // name a new thread after the thread that owned it ends.
    [[nodiscard]] static const void* calling_thread_identity_() noexcept {
        static thread_local const unsigned char identity_byte = 0;
        return &identity_byte;
    }

    // The record is process-wide because the global schema and kernel
    // tables have one writer.  It is a function-local static and not a
    // static data member, because a splice of a static data member has no
    // access check, and a splice cannot reach a local.
    using BrandRecord = detail::producer_claim::BrandRecord;

    [[nodiscard]] static BrandRecord& brand_record_() noexcept {
        static constinit BrandRecord record{};
        return record;
    }

    static void lock_brand_(BrandRecord& record) noexcept {
        while (record.lock.test_and_set(std::memory_order_acquire)) CRUCIBLE_SPIN_PAUSE;
    }

    static void unlock_brand_(BrandRecord& record) noexcept { record.lock.clear(std::memory_order_release); }

    // A thread id is not a lock-free atomic on every target, and a hidden
    // mutex on this check would put a lock on the dispatch path.
    static_assert(std::atomic<std::thread::id>::is_always_lock_free,
                  "std::atomic<std::thread::id> must be lock-free on this target.");
    std::atomic<std::thread::id> holder_{};
};

// The run-time half of a cold gate of a brand.  The context is evidence
// that a claim of its brand was won, but a reference to the context can
// reach another thread.  This check ends the process when a claim of the
// brand is live and the calling thread does not hold it.  It reads the
// thread identity, so a gate that runs for each op does not call it.
template <class Brand>
void require_brand_thread(ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> const&) noexcept {
    CRUCIBLE_FATAL_INVARIANT(ProducerClaim<Brand>::can_caller_use_brand());
}

}  // namespace host

// The test door of the foreground context.  scripts/check-ctx-testing-boundary.py
// refuses a use of it in code that ships, as it does for the door in
// Effect.h.
namespace testing {

struct ForegroundWitness {
    [[nodiscard]] static constexpr auto fg() noexcept -> ExecCtx<ctx_cap::Fg, Row<>> {
        return mint_foreground_context(detail::ctx_mint::fg_key{});
    }

    template <class Brand>
    [[nodiscard]] static constexpr auto branded_fg() noexcept -> ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> {
        return ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>>{detail::ctx_mint::fg_key{}};
    }
};

// With no brand, the unbranded context; with a brand, the context a
// claim of that brand would mint.
template <class Brand = void>
[[nodiscard]] inline constexpr auto foreground() noexcept {
    if constexpr (std::is_void_v<Brand>) {
        return ForegroundWitness::fg();
    } else {
        return ForegroundWitness::branded_fg<Brand>();
    }
}

}  // namespace testing

// A type is an execution context when it is a specialization of ExecCtx
// with the structure that the primary template gives it:
//
//   - a capability source and a row that WellFormedExecCtx admits
//   - cap_type and row_type naming exactly those two arguments
//   - exactly two data members, both private: the source, then the row.
//
// The check reads that structure by reflection.  A name match alone
// admits an explicit specialization of an unused row spelling, which
// replaces the whole class: it can have a public default constructor and
// no source at all, and every ctx-bound gate then admits the forged
// scope.  A specialization that passes the check holds a real capability
// source, and no route builds a source without its key.  Complexity: one
// walk over two data members for each context type.
//
// The walk reads every member with unchecked access, because the members
// it asks about are private.  It reads the type and the access of each
// member, and no splice follows, so no private state is written.
//
// Top-level cv and reference are stripped before the check, so a concept
// fed a forwarding-reference deduction still recognizes the context.
namespace detail {

template <class Cap, class R>
[[nodiscard]] consteval bool has_exec_ctx_structure_() noexcept {
    using Ctx = ExecCtx<Cap, R>;
    if constexpr (!WellFormedExecCtx<Cap, R>) {
        return false;
    } else if constexpr (!requires {
                             typename Ctx::cap_type;
                             typename Ctx::row_type;
                         }) {
        return false;
    } else if constexpr (!std::is_same_v<typename Ctx::cap_type, Cap> || !std::is_same_v<typename Ctx::row_type, R>) {
        return false;
    } else {
        static constexpr auto members =
            std::define_static_array(std::meta::nonstatic_data_members_of(^^Ctx, std::meta::access_context::unchecked()));
        constexpr std::meta::info expected_types[] = {std::meta::dealias(^^Cap), std::meta::dealias(^^R)};
        if (members.size() != std::size(expected_types)) return false;
        for (std::size_t index = 0; index < members.size(); ++index) {
            if (!std::meta::is_private(members[index])) return false;
            if (std::meta::dealias(std::meta::type_of(members[index])) != expected_types[index]) return false;
        }
        return true;
    }
}

template <class T>
[[nodiscard]] consteval bool is_exec_ctx_() noexcept {
    constexpr std::meta::info type = std::meta::dealias(^^T);
    if constexpr (!std::meta::has_template_arguments(type) || std::meta::template_of(type) != ^^ExecCtx) {
        return false;
    } else {
        static constexpr auto arguments = std::define_static_array(std::meta::template_arguments_of(type));
        return has_exec_ctx_structure_<typename[:arguments[0]:], typename[:arguments[1]:]>();
    }
}

}  // namespace detail

template <class T>
inline constexpr bool is_exec_ctx_v = detail::is_exec_ctx_<std::remove_cvref_t<T>>();
template <class T>
concept IsExecCtx = is_exec_ctx_v<T>;

template <IsExecCtx Ctx>
using cap_type_of_t = typename Ctx::cap_type;
template <IsExecCtx Ctx>
using row_type_of_t = typename Ctx::row_type;

// A body that claims row R may run in a context whose row covers R.
template <class Ctx, class R>
concept CtxAdmits = IsExecCtx<Ctx> && IsEffectRow<R> && Subrow<R, row_type_of_t<Ctx>>;

template <class Ctx, Effect Cap>
concept CtxOwnsCapability = IsExecCtx<Ctx> && row_contains_v<row_type_of_t<Ctx>, Cap>;

// The two named lifts below cost exactly what writing the fold by hand
// costs.  What they buy is that a reviewer recognizes the shape of an
// authorization at a glance, and that a rename of the row extractor
// reaches every site through one definition.
template <class Ctx, Effect... Es>
concept CtxOwnsAnyOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> || ...);

template <class Ctx, Effect... Es>
concept CtxOwnsAllOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> && ...);

// This asks what the capability source could authorize, not what the
// context currently claims.  The two differ: a background context may
// claim only two effects while its source permits four, and the
// context can widen into them.
template <class Ctx, Effect E>
concept CtxCanMint = IsExecCtx<Ctx> && row_contains_v<cap_permitted_row_t<cap_type_of_t<Ctx>>, E>;

namespace detail::ctx_witnesses {

// Witnesses in the shape of the named contexts the layer above defines
// (fixy/Ctx.h).  They are scaffolding, not a second spelling of those
// contexts.
//
// They live in the header rather than in a test because Capability.h,
// Computation.h, Permission.h and the fixtures of all three name them.
// The assertions below are invariants of the shipped concepts read
// against these shapes.  The scenarios that once sat here — building a
// promotion chain, probing with_cap, driving every operation at run
// time — moved to test/foundation/test_ctx.cpp, which is why this
// namespace no longer says self_test.
using FgWitness = ExecCtx<ctx_cap::Fg, Row<>>;
using BgWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>;
using BgIoWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>;
using BgBlockWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>;
using InitWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using InitBlockWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>;
using TestWitnessCtx = ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;

// The five named contexts of include/crucible/effects/_ExecCtx.h, with
// the six policy axes this layer dropped removed and the two that
// survive — the capability source and the row — kept exactly.  The
// port handed these to fixy/Ctx.h by name and recorded their rows nowhere
// that deleting the old tree would not erase; this is that record.
// Each is one witness above, and the assertion after each restates the
// row the old tree declared, so the layer that promotes them to
// production contexts starts from a declaration rather than from
// memory.  The comment on each is the old tree's.
//
// The two load contexts are not in the old tree.  Each claims Block on
// top of a row of the old tree, and the comment on each is new.

// The context of the foreground thread that runs dispatch.
using HotFgCtx = FgWitness;
static_assert(std::is_same_v<HotFgCtx, ExecCtx<ctx_cap::Fg, Row<>>>);

using BgDrainCtx = BgWitness;
static_assert(std::is_same_v<BgDrainCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>>);

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = BgIoWitness;
static_assert(std::is_same_v<BgCompileCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>>);

// The load context claims Block on top of the compile row.  Work that
// enters the kernel and waits there, such as a BPF program load waiting
// on the verifier, needs the atom the other two background rows omit.
// The background capability permits all four atoms, so this is the
// widest row a background context can claim.
using BgLoadCtx = BgBlockWitness;
static_assert(std::is_same_v<BgLoadCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>>);

// The context of process startup, before the threads are pinned.
using ColdInitCtx = InitWitness;
static_assert(std::is_same_v<ColdInitCtx, ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>>);

// The startup load context claims Block on top of the cold init row.
// Startup work that waits in the kernel, such as a BPF program load that
// waits for the verifier, needs that atom.  The init capability permits
// all four atoms, so this is the widest row an init context can claim.
using InitLoadCtx = InitBlockWitness;
static_assert(std::is_same_v<InitLoadCtx, ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>>);

// A fixture may claim any effect this row names, and no others.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = TestWitnessCtx;
static_assert(
    std::is_same_v<TestRunnerCtx, ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>>);

static_assert(sizeof(ExecCtx<>) == 1, "Both axes of ExecCtx are empty types, so the whole context must be 1 byte.");
static_assert(sizeof(FgWitness) == 1);
static_assert(sizeof(BgWitness) == 1);
static_assert(sizeof(BgIoWitness) == 1);
static_assert(sizeof(BgBlockWitness) == 1);
static_assert(sizeof(InitWitness) == 1);
static_assert(sizeof(InitBlockWitness) == 1);
static_assert(sizeof(TestWitnessCtx) == 1);

// A context that holds a capability source is evidence, so no route
// builds one without a constructor.  The background, init and test
// sources have no trivial constructor, so the copy of the context is not
// trivial either, and the one constructor that is not a copy takes a
// source.  std::bit_cast and std::start_lifetime_as then refuse the
// context.
template <class Ctx>
inline constexpr bool is_context_forgeable_v = std::is_trivially_copyable_v<Ctx> || std::is_implicit_lifetime_v<Ctx>;

static_assert(!is_context_forgeable_v<BgWitness> && !is_context_forgeable_v<BgBlockWitness>
                  && !is_context_forgeable_v<InitWitness> && !is_context_forgeable_v<InitBlockWitness>
                  && !is_context_forgeable_v<TestWitnessCtx>,
              "An execution context over Bg, Init or Test must have no trivial constructor, or std::bit_cast "
              "builds it from a byte and every ctx-bound gate admits the forged scope.");

// The foreground source keeps a trivial copy constructor, so that the
// hot path passes the context in no register.  That leaves it an
// implicit-lifetime type, and the annotation on the source is what the
// checked lifetime start refuses.  std::bit_cast is refused by the
// user-provided copy assignment, and the default constructor is gone.
static_assert(!std::is_trivially_copyable_v<FgWitness>,
              "The foreground context must not be trivially copyable, or std::bit_cast builds it from a byte.");
static_assert(std::is_trivially_copy_constructible_v<FgWitness>,
              "The foreground context must copy trivially, so that the hot path passes it for free.");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<FgWitness>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<ctx_cap::Fg>,
              "The checked lifetime start must refuse the foreground context and its source.");
static_assert(!std::is_default_constructible_v<FgWitness> && !std::is_default_constructible_v<ctx_cap::Fg>
                  && !std::is_default_constructible_v<detail::ctx_mint::fg_key>,
              "A foreground context is built from the key of the producer claim, never from nothing.");

// A branded foreground context obeys every rule of the unbranded one, and
// its source is built only by the claim of its brand.  It passes a gate
// that asks for no brand, and no gate of another brand.
struct BrandWitness {};
struct OtherBrandWitness {};
using BrandedFgWitness = ExecCtx<ctx_cap::BrandedFg<BrandWitness>, Row<>>;
using OtherBrandedFgWitness = ExecCtx<ctx_cap::BrandedFg<OtherBrandWitness>, Row<>>;
static_assert(IsCapType<ctx_cap::BrandedFg<BrandWitness>> && IsBrandedForeground<ctx_cap::BrandedFg<BrandWitness>>);
static_assert(!IsBrandedForeground<ctx_cap::Fg> && !IsBrandedForeground<Bg> && !IsBrandedForeground<int>);
static_assert(sizeof(BrandedFgWitness) == 1);
static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::BrandedFg<BrandWitness>>, Row<>>);
static_assert(!std::is_copy_constructible_v<BrandedFgWitness> && !std::is_move_constructible_v<BrandedFgWitness>
                  && !std::is_copy_assignable_v<BrandedFgWitness> && !std::is_move_assignable_v<BrandedFgWitness>,
              "A branded foreground context must have no copy and no move, or a by-value capture or a thread "
              "argument takes it to a thread that holds no claim.");
static_assert(!std::is_trivially_copyable_v<BrandedFgWitness>
                  && !std::is_trivially_copyable_v<ctx_cap::BrandedFg<BrandWitness>>,
              "A branded foreground context must not be trivially copyable, or std::bit_cast builds it from a byte.");
static_assert(!std::is_constructible_v<BrandedFgWitness, detail::ctx_mint::fg_key>,
              "Only a producer claim and the test witness build a branded context from the key.");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<BrandedFgWitness>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<ctx_cap::BrandedFg<BrandWitness>>,
              "The checked lifetime start must refuse the branded foreground context and its source.");
static_assert(!std::is_default_constructible_v<BrandedFgWitness>
                  && !std::is_default_constructible_v<ctx_cap::BrandedFg<BrandWitness>>
                  && !std::is_constructible_v<ctx_cap::BrandedFg<BrandWitness>, detail::ctx_mint::fg_key>,
              "A branded source is built only by the claim of its brand.");
static_assert(!std::is_constructible_v<FgWitness, BrandedFgWitness>,
              "No route forgets a brand.  Any translation unit can declare a brand and hold its claim, so a "
              "context with no brand names no single-producer state and is not evidence.");
static_assert(!std::is_constructible_v<BrandedFgWitness, FgWitness>, "No route adds a brand to a context.");
static_assert(!std::is_constructible_v<OtherBrandedFgWitness, BrandedFgWitness>,
              "A context of one brand passes no gate of another brand.");
static_assert(!std::is_default_constructible_v<host::ProducerClaim<BrandWitness>>,
              "Only the brand builds its producer claim.");
static_assert(!std::is_trivially_copyable_v<host::ProducerClaim<BrandWitness>>
                  && !std::is_implicit_lifetime_v<host::ProducerClaim<BrandWitness>>,
              "No producer claim is built from bytes or started over a buffer.");
static_assert(!std::is_constructible_v<ctx_cap::Fg, const ctx_cap::BrandedFg<BrandWitness>&>,
              "A branded source does not become the unbranded source.");

// Every axis defaults to the claim-nothing end of its range.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type, ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type, Row<>>);

using BgVariantA = ExecCtx<Bg, Row<Effect::Bg>>;
using BgVariantB = ExecCtx<Bg, Row<Effect::Bg, Effect::IO>>;
static_assert(!std::is_same_v<BgVariantA, BgVariantB>, "Two contexts that differ in row must be distinct types");

static_assert(is_cap_type_v<ctx_cap::Fg>);
static_assert(is_cap_type_v<Bg>);
static_assert(is_cap_type_v<Init>);
static_assert(is_cap_type_v<Test>);
static_assert(!is_cap_type_v<int>);
static_assert(!is_cap_type_v<void*>);

static_assert(is_effect_row_v<Row<>>);
static_assert(is_effect_row_v<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!is_effect_row_v<int>);
static_assert(is_effect_row_v<Row<> const>);
static_assert(is_effect_row_v<Row<>&>);
static_assert(is_effect_row_v<Row<Effect::Bg> const&>);
static_assert(is_effect_row_v<Row<Effect::Bg>&&>);
static_assert(!is_effect_row_v<int const&>);
static_assert(IsEffectRow<Row<Effect::Bg> const&>);
static_assert(IsEffectRow<Row<Effect::Bg>&&>);

static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::Fg>, Row<>>);
static_assert(std::is_same_v<cap_permitted_row_t<Bg>, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Init>, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Test>, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>);

// The witnesses already satisfy this, or their own declarations would
// have failed.  Restating it here catches a later rewrite.
static_assert(Subrow<typename FgWitness::row_type, cap_permitted_row_t<typename FgWitness::cap_type>>);
static_assert(Subrow<typename BgWitness::row_type, cap_permitted_row_t<typename BgWitness::cap_type>>);
static_assert(Subrow<typename BgIoWitness::row_type, cap_permitted_row_t<typename BgIoWitness::cap_type>>);
static_assert(Subrow<typename InitWitness::row_type, cap_permitted_row_t<typename InitWitness::cap_type>>);
static_assert(Subrow<typename InitBlockWitness::row_type, cap_permitted_row_t<typename InitBlockWitness::cap_type>>);
static_assert(Subrow<typename TestWitnessCtx::row_type, cap_permitted_row_t<typename TestWitnessCtx::cap_type>>);

static_assert(!WellFormedExecCtx<ctx_cap::Fg, Row<Effect::Bg>>,
              "A foreground source permits the empty row only; a context claiming Bg on it is ill-formed.");
static_assert(!WellFormedExecCtx<ctx_cap::Fg, Row<Effect::Block>>,
              "A foreground source never permits Block, so the hot path cannot claim it.");
static_assert(WellFormedExecCtx<Init, Row<Effect::Init, Effect::Block>>,
              "An init source permits Block, because process startup waits in the kernel.");
static_assert(!WellFormedExecCtx<Init, Row<Effect::Bg>>, "An init source cannot stand in for a background one.");
static_assert(!WellFormedExecCtx<Test, Row<Effect::Bg>>, "A test source cannot stand in for a background one.");
static_assert(!WellFormedExecCtx<int, Row<>>);

static_assert(is_exec_ctx_v<FgWitness>);
static_assert(is_exec_ctx_v<BgWitness>);
static_assert(!is_exec_ctx_v<int>);
static_assert(!is_exec_ctx_v<Bg>);
static_assert(is_exec_ctx_v<FgWitness const>);
static_assert(is_exec_ctx_v<FgWitness&>);
static_assert(is_exec_ctx_v<FgWitness const&>);
static_assert(is_exec_ctx_v<FgWitness&&>);
static_assert(is_exec_ctx_v<BgWitness const&>);
static_assert(!is_exec_ctx_v<int const&>);
static_assert(!is_exec_ctx_v<Bg const&>);
static_assert(IsExecCtx<FgWitness const&>);
static_assert(IsExecCtx<FgWitness&&>);

static_assert(std::is_same_v<cap_type_of_t<BgWitness>, Bg>);
static_assert(std::is_same_v<row_type_of_t<BgWitness>, Row<Effect::Bg, Effect::Alloc>>);
static_assert(std::is_same_v<row_type_of_t<HotFgCtx>, Row<>>);
static_assert(std::is_same_v<row_type_of_t<BgCompileCtx>, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<cap_type_of_t<ColdInitCtx>, Init>);
static_assert(std::is_same_v<cap_type_of_t<TestRunnerCtx>, Test>);

static_assert(std::is_same_v<ctx_cap::Bg, Bg>);
static_assert(std::is_same_v<ctx_cap::Init, Init>);
static_assert(std::is_same_v<ctx_cap::Test, Test>);

static_assert(CtxAdmits<FgWitness, Row<>>);
static_assert(!CtxAdmits<FgWitness, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgWitness, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgWitness, Row<Effect::Bg, Effect::Alloc>>);
static_assert(!CtxAdmits<BgWitness, Row<Effect::IO>>);
static_assert(CtxAdmits<BgIoWitness, Row<Effect::IO>>);
static_assert(CtxAdmits<TestWitnessCtx, Row<Effect::Block>>);

static_assert(CtxOwnsCapability<BgWitness, Effect::Bg>);
static_assert(CtxOwnsCapability<BgWitness, Effect::Alloc>);
static_assert(!CtxOwnsCapability<BgWitness, Effect::IO>);
static_assert(CtxOwnsCapability<BgIoWitness, Effect::IO>);
static_assert(!CtxOwnsCapability<FgWitness, Effect::Bg>);
static_assert(!CtxOwnsCapability<InitWitness, Effect::Block>);
static_assert(CtxOwnsCapability<InitLoadCtx, Effect::Block>);

static_assert(CtxOwnsAnyOf<BgWitness, Effect::Bg, Effect::IO>,
              "The disjunctive lift must accept a row that carries one of the named atoms.");
static_assert(CtxOwnsAnyOf<InitWitness, Effect::Bg, Effect::Init>);
static_assert(!CtxOwnsAnyOf<FgWitness, Effect::Bg, Effect::IO, Effect::Init>,
              "The disjunctive lift must reject an empty row.");
static_assert(!CtxOwnsAnyOf<BgWitness>, "The disjunctive lift over no atoms is false.");

static_assert(CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::Alloc>,
              "The conjunctive lift must accept a row that carries every named atom.");
static_assert(!CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::IO>,
              "The conjunctive lift must reject a row that is missing one of the named atoms.  Widening "
              "the row first is the way through.");
static_assert(CtxOwnsAllOf<BgWitness>, "The conjunctive lift over no atoms is true.");
static_assert(!CtxOwnsAllOf<FgWitness, Effect::Bg>,
              "The conjunctive lift must reject any non-empty pack against an empty row.");

// A one-atom lift must answer exactly what the single-atom concept
// answers, so that the two surfaces stay interchangeable.
static_assert(CtxOwnsAnyOf<BgWitness, Effect::Bg> == CtxOwnsCapability<BgWitness, Effect::Bg>);
static_assert(CtxOwnsAllOf<BgWitness, Effect::Bg> == CtxOwnsCapability<BgWitness, Effect::Bg>);

// The background witness claims two effects but its source permits
// four, so the next assertions differ from the ownership ones above.
// The init witness claims three and its source permits four in the same
// way.
static_assert(CtxCanMint<BgWitness, Effect::Alloc>);
static_assert(CtxCanMint<BgWitness, Effect::IO>);
static_assert(CtxCanMint<BgWitness, Effect::Block>);
static_assert(CtxCanMint<BgWitness, Effect::Bg>);
static_assert(!CtxCanMint<BgWitness, Effect::Init>);
static_assert(CtxCanMint<BgCompileCtx, Effect::Block>);
static_assert(CtxCanMint<InitWitness, Effect::Alloc>);
static_assert(CtxCanMint<InitWitness, Effect::IO>);
static_assert(CtxCanMint<InitWitness, Effect::Block>);
static_assert(!CtxCanMint<InitWitness, Effect::Bg>);
static_assert(!CtxCanMint<FgWitness, Effect::Alloc>);
static_assert(!CtxCanMint<FgWitness, Effect::Bg>);
static_assert(CtxCanMint<TestWitnessCtx, Effect::Block>);

}  // namespace detail::ctx_witnesses

}  // namespace foundation::effects
