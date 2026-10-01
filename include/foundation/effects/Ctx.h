#pragma once

// An execution context is the scope an operation runs in: which
// capability source the caller holds, and which effects the scope has
// claimed.  A body that claims row R may run in a context whose row
// covers R, and a context may claim no more than its capability source
// permits.  That pair is the whole of reject-by-default for effects: a
// context minted with the empty row admits nothing effectful, and every
// relaxation is a wider row named in the type.
//
// A context carries a capability source and a row, and no other axis: no
// NUMA policy, allocator class, heat tier, cache residency, workload hint
// or progress class.  A caller that wants to say how hot a path is or
// which allocator it reaches for grades the value with the band wrapper
// for that axis.  A channel or a fork that wants a budget takes it as its
// own parameter.  A context describes the surrounding scope, not a value.

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

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
class[[= ::foundation::lifetime::no_start_over_bytes{}]] BrandedFg {
public:
    using brand_type = Brand;

    template <template <Effect...> class R>
    using permitted_as = R<>;

    BrandedFg(const BrandedFg&) = delete("a copy of a branded source can go to a thread that holds no claim");
    BrandedFg(BrandedFg&&) = delete("a moved branded source can go to a thread that holds no claim");
    BrandedFg&
    operator=(const BrandedFg&) = delete("a copy of a branded source can go to a thread that holds no claim");
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

class[[= ::foundation::lifetime::no_start_over_bytes{}]] Fg {
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
// A branded source is recognized by reflection on its template, in a
// function at namespace scope that is not a template, so no translation
// unit can specialize the test.
[[nodiscard]] consteval bool is_branded_foreground(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == ^^ctx_cap::BrandedFg;
}

template <class T>
concept IsBrandedForeground = is_branded_foreground(^^T);

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
    // copies.  The source authorizes every atom it permits, and a holder
    // of it mints any of them and builds a context of any row it permits.
    // So a context lends its source only when its row already claims all
    // of them.  A narrower context keeps its source, and nothing reached
    // from it claims an atom that its row does not.
    [[nodiscard]] constexpr Cap const& cap() const noexcept
        requires Subrow<cap_permitted_row_t<Cap>, Row>
    {
        return cap_;
    }

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

    // A context narrows its row with no evidence, because the narrower row
    // claims less.  It never widens.  A wider row claims an atom that this
    // context does not, and the evidence for that atom is the source
    // itself, held by value: ExecCtx<Cap, Wider>{source}.  cap() lends the
    // source only from a context that already claims every atom of it, so
    // that evidence never comes out of a narrower context.
    template <class NewRow>
        requires IsEffectRow<NewRow> && Subrow<NewRow, Row>
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

// The record of one brand (host::ProducerClaim below): the thread that holds
// the live claims of the brand, and how many it holds.  The record names the
// brand by its stable id, and zero names no brand.  A record takes its brand
// under the lock of the registry, and it keeps the brand for the life of the
// process.  The type holds no key and builds no context.
struct BrandRecord {
    std::atomic<std::uint64_t> brand{0};
    std::atomic<std::uintptr_t> holder{0};
    std::size_t claims = 0;
};

// The number of brands that one process can claim.  The claim of one more
// brand ends the process.
inline constexpr std::size_t brand_capacity = 64;

// The records of every brand.  The claims fill them in order, and a record
// never becomes free, so a scan can stop at the first record with no brand.
struct BrandRegistry {
    std::atomic_flag lock{};
    std::array<BrandRecord, brand_capacity> records{};
};

// One registry for the process.  A claim won in one shared library and the
// brand check in another read the same record.  A static of the claim
// template cannot do this: the instantiation for a brand takes the visibility
// of the brand, which is hidden, so each shared library would hold a copy.
CRUCIBLE_PROCESS_WIDE inline constinit BrandRegistry brand_registry{};

// The identity of the calling thread, the same in every shared library: the
// address of the thread control block, which the thread pointer register
// holds.  Each live thread has its own block, so two live threads never have
// one identity, and one thread keeps its identity for its life.  The block
// of a live thread is never at address zero, so zero names no thread.  Like
// a std::thread::id, a value can name a new thread after the thread that
// owned it ends, because the runtime can give its block to the new thread.
[[nodiscard]] inline std::uintptr_t calling_thread_identity() noexcept {
    return std::bit_cast<std::uintptr_t>(__builtin_thread_pointer());
}

}  // namespace detail::producer_claim

namespace host {

// The one production builder of the foreground key.  Its member is
// private, and the producer claims below are its only friends.
struct ForegroundOwner final {
private:
    [[nodiscard]] static constexpr auto key() noexcept -> detail::ctx_mint::fg_key {
        return detail::ctx_mint::fg_key{};
    }

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
        if (entered_record_ != nullptr) leave_brand_(*entered_record_);
    }

    // The part that inlines is a relaxed load, a comparison and a branch.
    // The claim and the failure report sit out of line and cold.  The body
    // reads the thread identity and an atomic, so it is not constexpr.
    [[nodiscard]] CRUCIBLE_INLINE auto mint_producer_context() noexcept  // MINT-PATTERN-OK: reads the thread id
        -> ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> {
        const std::uintptr_t current_tid = detail::producer_claim::calling_thread_identity();
        if (holder_.load(std::memory_order_relaxed) != current_tid) [[unlikely]]
            claim_or_reject_(current_tid);
        return ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>>{ForegroundOwner::key()};
    }

    // True when no thread holds the claim yet, or the calling thread
    // holds it.  Two threads that both ask before either claims both read
    // true, and the loser of the claim then ends the process.
    [[nodiscard]] bool is_claimable_by_caller() const noexcept {
        const std::uintptr_t holder = holder_.load(std::memory_order_relaxed);
        return holder == 0 || holder == detail::producer_claim::calling_thread_identity();
    }

    // True when no claim of this brand is live, or the calling thread
    // holds the live claims.  A cold gate of the brand asks this through
    // require_brand_thread below.
    [[nodiscard]] static bool can_caller_use_brand() noexcept {
        const BrandRecord* const record = find_brand_record_();
        if (record == nullptr) return true;
        const std::uintptr_t holder = record->holder.load(std::memory_order_acquire);
        return holder == 0 || holder == detail::producer_claim::calling_thread_identity();
    }

private:
    constexpr ProducerClaim() noexcept = default;

    friend Brand;

    using BrandRecord = detail::producer_claim::BrandRecord;
    using BrandRegistry = detail::producer_claim::BrandRegistry;

    // The key of the brand in the process-wide registry.  A brand needs a
    // stable identity, because the key must be the same in every shared
    // library: stable_type_id refuses a brand with internal linkage or with
    // no declared name, and it names the reason.
    [[nodiscard]] static consteval std::uint64_t brand_key_() noexcept {
        constexpr std::uint64_t key = ::foundation::reflect::stable_type_id<Brand>;
        static_assert(key != 0, "zero names no brand in the registry of producer claims");
        return key;
    }

    [[gnu::cold, gnu::noinline]] void claim_or_reject_(std::uintptr_t current_tid) noexcept {
        std::uintptr_t holder = holder_.load(std::memory_order_relaxed);
        if (holder == 0) {
            // Relaxed is enough: the claim synchronizes nothing, it only
            // records which thread arrived first.  A failed exchange leaves
            // the winner's id in `holder`, which the check below reports.
            if (holder_.compare_exchange_strong(holder, current_tid, std::memory_order_relaxed)) {
                BrandRecord* const record = enter_brand_(current_tid);
                if (record != nullptr) {
                    entered_record_ = record;
                    return;
                }
                // Another thread holds a live claim of this brand.  The claim
                // is undone first, so that its destructor removes no entry
                // that it did not make.
                holder_.store(0, std::memory_order_relaxed);
                CRUCIBLE_FATAL_INVARIANT(record != nullptr);
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
    // destructor write a record, so the lock of the registry is never on the
    // path that runs for each op.  Null when another thread holds a live
    // claim of the brand.  Complexity: linear in brand_capacity.
    [[gnu::cold, gnu::noinline]] static BrandRecord* enter_brand_(std::uintptr_t thread_identity) noexcept {
        BrandRegistry& registry = detail::producer_claim::brand_registry;
        lock_registry_(registry);
        BrandRecord* record = nullptr;
        for (BrandRecord& candidate : registry.records) {
            const std::uint64_t brand = candidate.brand.load(std::memory_order_relaxed);
            if (brand == brand_key_() || brand == 0) {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr) [[unlikely]] {
            unlock_registry_(registry);
            // More brands than brand_capacity hold producer claims.
            CRUCIBLE_FATAL_INVARIANT(record != nullptr);
            return nullptr;
        }
        record->brand.store(brand_key_(), std::memory_order_release);
        const bool is_only_brand_thread =
            record->claims == 0 || record->holder.load(std::memory_order_relaxed) == thread_identity;
        if (is_only_brand_thread) {
            record->holder.store(thread_identity, std::memory_order_release);
            ++record->claims;
        }
        unlock_registry_(registry);
        return is_only_brand_thread ? record : nullptr;
    }

    [[gnu::cold, gnu::noinline]] static void leave_brand_(BrandRecord& record) noexcept {
        BrandRegistry& registry = detail::producer_claim::brand_registry;
        lock_registry_(registry);
        const bool has_entry = record.claims != 0;
        if (has_entry && --record.claims == 0) record.holder.store(0, std::memory_order_release);
        unlock_registry_(registry);
        CRUCIBLE_FATAL_INVARIANT(has_entry);
    }

    // The record of the brand, or null when no claim of the brand was ever
    // won.  It reads with no lock: a record takes its brand once, with a
    // release store, and keeps it.  Complexity: linear in brand_capacity.
    [[nodiscard]] static const BrandRecord* find_brand_record_() noexcept {
        for (const BrandRecord& candidate : detail::producer_claim::brand_registry.records) {
            const std::uint64_t brand = candidate.brand.load(std::memory_order_acquire);
            if (brand == brand_key_()) return &candidate;
            if (brand == 0) return nullptr;
        }
        return nullptr;
    }

    static void lock_registry_(BrandRegistry& registry) noexcept {
        while (registry.lock.test_and_set(std::memory_order_acquire))
            CRUCIBLE_SPIN_PAUSE;
    }

    static void unlock_registry_(BrandRegistry& registry) noexcept { registry.lock.clear(std::memory_order_release); }

    // The identity of the thread that holds the claim, or zero.  An atomic
    // word is lock-free on every target of the tree, so the check on the
    // dispatch path takes no hidden mutex.
    std::atomic<std::uintptr_t> holder_{0};

    // The record that the win of this claim entered, or null before a win.
    // The destructor leaves that record without a second scan of the
    // registry.  Only the winning thread writes the pointer, one time, after
    // its entry.  test/foundation/test_producer_claim_across_libraries.cpp and
    // test/test_process_wide_across_libraries.cpp win a claim in one shared
    // library and read or destroy it in another.
    BrandRecord* entered_record_ = nullptr;
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

// The test door of the foreground context.  utils/scripts/check-ctx-testing-boundary.py
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
//
// The check is a function that is not a template, and the concept reads
// it directly.  No translation unit can specialize either.  A variable
// template or a function template in their place is a door: an explicit
// specialization of it skips the structure test, and a plain struct with a
// row_type then passes every ctx-bound gate.
namespace detail {

// True when the class has a public member type with the identifier, and
// that member names the expected type.
[[nodiscard]] consteval bool names_member_type_(std::meta::info type, std::string_view identifier,
                                                std::meta::info expected) {
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::current())) {
        if (!std::meta::is_type(member) || !std::meta::has_identifier(member)) continue;
        if (std::meta::identifier_of(member) != identifier) continue;
        return std::meta::dealias(member) == std::meta::dealias(expected);
    }
    return false;
}

}  // namespace detail

[[nodiscard]] consteval bool is_exec_ctx(std::meta::info spelled) {
    const std::meta::info type = std::meta::dealias(std::meta::remove_cvref(spelled));
    if (!std::meta::has_template_arguments(type) || std::meta::template_of(type) != ^^ExecCtx) return false;
    const std::vector<std::meta::info> arguments = std::meta::template_arguments_of(type);
    const std::meta::info cap = arguments[0];
    const std::meta::info row = arguments[1];
    if (!std::meta::extract<bool>(std::meta::substitute(^^WellFormedExecCtx, {cap, row}))) return false;
    if (!detail::names_member_type_(type, "cap_type", cap) || !detail::names_member_type_(type, "row_type", row)) {
        return false;
    }
    const std::vector<std::meta::info> members =
        std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked());
    const std::meta::info expected_types[] = {std::meta::dealias(cap), std::meta::dealias(row)};
    if (members.size() != std::size(expected_types)) return false;
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (!std::meta::is_private(members[index])) return false;
        if (std::meta::dealias(std::meta::type_of(members[index])) != expected_types[index]) return false;
    }
    return true;
}

template <class T>
concept IsExecCtx = is_exec_ctx(^^T);

template <IsExecCtx Ctx>
using cap_type_of_t = typename Ctx::cap_type;
template <IsExecCtx Ctx>
using row_type_of_t = typename Ctx::row_type;

// A body that claims row R may run in a context whose row covers R.
template <class Ctx, class R>
concept CtxAdmits = IsExecCtx<Ctx> && IsEffectRow<R> && Subrow<R, row_type_of_t<Ctx>>;

template <class Ctx, Effect Cap>
concept CtxOwnsCapability = IsExecCtx<Ctx> && row_contains(^^row_type_of_t<Ctx>, Cap);

// The two named lifts below cost exactly what writing the fold by hand
// costs.  What they buy is that a reviewer recognizes the shape of an
// authorization at a glance, and that a rename of the row extractor
// reaches every site through one definition.
template <class Ctx, Effect... Es>
concept CtxOwnsAnyOf = IsExecCtx<Ctx> && (row_contains(^^row_type_of_t<Ctx>, Es) || ...);

template <class Ctx, Effect... Es>
concept CtxOwnsAllOf = IsExecCtx<Ctx> && (row_contains(^^row_type_of_t<Ctx>, Es) && ...);

namespace detail::ctx_witnesses {

// Witnesses in the shape of the named contexts the layer above defines
// (fixy/Ctx.h).  They are scaffolding, not a second spelling of those
// contexts.
//
// They live in the header rather than in a test because fixy/Ctx.h,
// Permission.h, the check files of this layer and many tests and
// fixtures name them.  The check file of this header holds the
// assertions, which are invariants of the shipped concepts read against
// these shapes.  test/foundation/test_ctx.cpp builds a promotion chain,
// probes with_cap and drives every operation at run time.
using FgWitness = ExecCtx<ctx_cap::Fg, Row<>>;
using BgWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>;
using BgIoWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>;
using BgBlockWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>;
using InitWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using InitBlockWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>;
using TestWitnessCtx = ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;

// The seven named contexts.  Each has two axes, the capability source
// and the row.  Each is one witness above.  The check file of this
// header states the row of each, so the layer that promotes them to
// production contexts starts from a declaration.

// The context of the foreground thread that runs dispatch.
using HotFgCtx = FgWitness;

using BgDrainCtx = BgWitness;

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = BgIoWitness;

// The load context claims Block on top of the compile row.  Work that
// enters the kernel and waits there, such as a BPF program load waiting
// on the verifier, needs the atom the other two background rows omit.
// The background capability permits all four atoms, so this is the
// widest row a background context can claim.
using BgLoadCtx = BgBlockWitness;

// The context of process startup, before the threads are pinned.
using ColdInitCtx = InitWitness;

// The startup load context claims Block on top of the cold init row.
// Startup work that waits in the kernel, such as a BPF program load that
// waits for the verifier, needs that atom.  The init capability permits
// all four atoms, so this is the widest row an init context can claim.
using InitLoadCtx = InitBlockWitness;

// A fixture may claim any effect this row names, and no others.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = TestWitnessCtx;

}  // namespace detail::ctx_witnesses

}  // namespace foundation::effects
