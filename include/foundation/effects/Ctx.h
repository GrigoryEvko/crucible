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

#include <foundation/reflect/Members.h>
#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/core/FpMode.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/os/FpControl.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
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
//
// The source belongs to the thread that holds the claim of its brand, so it
// neither moves to another thread nor gives a reference to two threads
// (foundation/Transfer.h).  A class that holds a reference to it is neither
// Sendable nor Shareable, and a door that starts a thread refuses it.
template <class Brand>
class[[= ::foundation::lifetime::no_start_over_bytes{}]][[= ::foundation::transfer::not_sendable{}]]
     [[= ::foundation::transfer::not_shareable{}]] BrandedFg {
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

// The source of a kernel context.  A kernel runs on one thread, under the
// FP mode Mode of the register of that thread, and the source permits the
// empty row: no allocation, no IO, no block and no Nondet.  The source keeps
// the epoch of the FP scope that it came from and the thread of the mint
// (foundation/os/FpControl.h).  It has no copy and no move, as a branded
// source has none.  Only the kernel context builds it, and only
// mint_kernel_context builds a kernel context.
template <class Brand, ::foundation::core::FpMode Mode>
class[[= ::foundation::lifetime::no_start_over_bytes{}]] KernelSource {
public:
    using brand_type = Brand;
    static constexpr ::foundation::core::FpMode fp_mode = Mode;

    template <template <Effect...> class R>
    using permitted_as = R<>;

    KernelSource(const KernelSource&) = delete("a copy of a kernel source can go to a thread whose register holds "
                                               "another mode");
    KernelSource(KernelSource&&) = delete("a moved kernel source can go to a thread whose register holds another "
                                          "mode");
    KernelSource& operator=(const KernelSource&) = delete("a kernel source names one scope of one thread");
    KernelSource& operator=(KernelSource&&) = delete("a kernel source names one scope of one thread");
    constexpr ~KernelSource() noexcept {}

    // The epoch of the FP scope of the mint, and the thread of the mint.
    [[nodiscard]] constexpr std::uint64_t fp_scope_epoch() const noexcept { return fp_scope_epoch_; }
    [[nodiscard]] constexpr std::uintptr_t thread() const noexcept { return thread_; }

private:
    constexpr KernelSource(std::uint64_t fp_scope_epoch, std::uintptr_t thread) noexcept
        : fp_scope_epoch_{fp_scope_epoch}, thread_{thread} {}

    friend class ::foundation::effects::ExecCtx<KernelSource, ::foundation::effects::Row<>>;

    std::uint64_t fp_scope_epoch_ = 0;
    std::uintptr_t thread_ = 0;
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

// A kernel source is recognized the same way.
[[nodiscard]] consteval bool is_kernel_source(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == ^^ctx_cap::KernelSource;
}

template <class T>
concept IsKernelSource = is_kernel_source(^^T);

template <class T>
concept IsCapType = std::same_as<T, ctx_cap::Fg> || IsBrandedForeground<T> || IsKernelSource<T> || IsContext<T>;

// The one door of a kernel context, declared here so that the context can
// name it as a friend.  The definition is below.
template <class Brand, ::foundation::core::FpMode Mode>
    requires ::foundation::os::EncodableFpMode<Mode>
[[nodiscard]] auto mint_kernel_context(::foundation::os::FpScope<Mode> const& scope) noexcept
    -> ExecCtx<ctx_cap::KernelSource<Brand, Mode>, Row<>>;
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
//
// The check and the concepts of the row come before the class, because the
// class names the tag door below as a friend, and the head of the door
// reads them.  Each one reads the class only when a gate evaluates it, and
// the class is complete then.
namespace detail {

// True when the class has a public member type with the identifier, and
// that member names the expected type.
[[nodiscard]] consteval bool names_member_type_(std::meta::info type, std::string_view identifier,
                                                std::meta::info expected) {
    const std::meta::info member =
        ::foundation::reflect::member_named_in(type, std::meta::access_context::current(), identifier);
    return member != std::meta::info{} && std::meta::is_type(member)
        && std::meta::dealias(member) == std::meta::dealias(expected);
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

// The bare tag of the value atom E, from a context whose row holds E.  The
// read of a clock reader and the pop of a merge take cap::Nondet in this
// form.  The context names this door as a friend, and the door copies the
// atom from the source of the context.  It makes no capability token, so it
// holds no contract check, and a noexcept body that calls it needs no
// terminate pad.  The definition is below the class.
template <Effect E, IsExecCtx Ctx>
    requires CtxOwnsCapability<Ctx, E> && HasBareTag<E>
[[nodiscard]] constexpr bare_tag_t<E> extract_bare_from_ctx(Ctx const& ctx) noexcept;

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
        requires(!IsBrandedForeground<Cap> && !IsKernelSource<Cap>)
        : cap_{cap} {}

private:
    // A branded source has no copy, so the context builds it in place from
    // the key.  This constructor is private, so only a producer claim and
    // the test witness build a branded context.
    constexpr explicit ExecCtx(detail::ctx_mint::fg_key key) noexcept
        requires IsBrandedForeground<Cap>
        : cap_{key} {}

    // A kernel source has no copy either, so the context builds it in place
    // from the epoch of a scope and a thread.  Only the mint of a kernel
    // context calls this constructor.
    constexpr ExecCtx(std::uint64_t fp_scope_epoch, std::uintptr_t thread) noexcept
        requires IsKernelSource<Cap>
        : cap_{fp_scope_epoch, thread} {}

    template <class Brand>
    friend class host::ProducerClaim;
    friend struct testing::ForegroundWitness;

    template <class Brand, ::foundation::core::FpMode Mode>
        requires ::foundation::os::EncodableFpMode<Mode>
    friend auto mint_kernel_context(::foundation::os::FpScope<Mode> const& scope) noexcept
        -> ExecCtx<ctx_cap::KernelSource<Brand, Mode>, ::foundation::effects::Row<>>;

    // The tag door reads the source of a context whose row does not claim
    // each atom of the source.  It copies only the atom that the row claims.
    template <Effect E, IsExecCtx Ctx>
        requires CtxOwnsCapability<Ctx, E> && HasBareTag<E>
    friend constexpr bare_tag_t<E> extract_bare_from_ctx(Ctx const& ctx) noexcept;

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

// The key of a brand in the registry: its stable type id.  A brand with no
// stable identity, or with the id zero, stops the constant evaluation.
// Zero marks a record with no brand, so it names no brand.
[[nodiscard]] consteval std::uint64_t brand_key_of(std::meta::info brand) {
    const std::uint64_t key = ::foundation::reflect::stable_type_id(brand);
    if (key == 0) {
        throw std::meta::exception(u8"zero names no brand in the registry of producer claims", brand);
    }
    return key;
}

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
// that asks for a context holds the claim until it gives the claim up
// (release_claim) or this object ends.  A request from any other thread
// while the claim is held ends the process.
//
// Only the brand builds its claim, and the context names the brand.  The
// single-producer state S holds a ProducerClaim<S>, so a context branded
// S is evidence that its holder runs on the thread that owns an S.  A
// thread that holds the claim of another state passes no gate of S.
//
// The claim belongs to the thread that holds it, so a claim, and each state
// that holds one, moves to no other thread and takes no write of another
// thread (thread_bound of foundation/Transfer.h).  A read through a reference
// to const, such as is_claimable_by_caller, is safe on each thread.
template <class Brand>
class[[= ::foundation::transfer::thread_bound{}]] ProducerClaim {
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
    [[nodiscard]] CRUCIBLE_INLINE auto mint_producer_context() noexcept -> ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> {
        const std::uintptr_t current_tid = detail::producer_claim::calling_thread_identity();
        if (holder_now_() != current_tid) [[unlikely]]
            claim_or_reject_(current_tid);
        return ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>>{ForegroundOwner::key()};
    }

    // Gives up the claim of the calling thread.  The next request for a
    // context claims again, from any thread.  With no claim, nothing changes.
    // A claim of another thread ends the process in each build.
    //
    // The release leaves the record of the brand first, and then frees the
    // holder with a release store.  A claim reads the free holder with an
    // acquire exchange, so each write of the last holder happens before each
    // access of the next holder, and the next holder finds the record free.
    // The release takes the lock of the registry, so it is cold.
    [[gnu::cold, gnu::noinline]] void release_claim() noexcept {
        const std::uintptr_t holder = holder_now_();
        if (holder == 0) return;
        // Not a contract clause, for the reason of claim_or_reject_.
        CRUCIBLE_FATAL_INVARIANT(holder == detail::producer_claim::calling_thread_identity());
        BrandRecord* const record = entered_record_;
        entered_record_ = nullptr;
        if (record != nullptr) leave_brand_(*record);
        free_holder_();
    }

    // True when no thread holds the claim yet, or the calling thread
    // holds it.  Two threads that both ask before either claims both read
    // true, and the loser of the claim then ends the process.
    [[nodiscard]] bool is_claimable_by_caller() const noexcept {
        const std::uintptr_t holder = holder_now_();
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

    // The proof that the caller is the one writer of the counters of the
    // single-producer state of the brand in the role Role (THE PROOF OF THE
    // ONE WRITER of foundation/SoleWriterProof.h).  The context is the
    // evidence: only mint_producer_context makes it, after the check of the
    // calling thread.  The door is private, so only the brand calls it, with
    // the context that it minted in the same call.  So the proof needs no
    // second check of the thread, and it costs no instruction.
    //
    // One thread holds all the live claims of a brand (enter_brand_), so a
    // proof holds for each state of the brand.  A thread that gives the claim
    // up frees the holder with a release store, and the next holder acquires
    // it, so each write of the last writer happens before each write of the
    // next one.
    template <class Role>
    [[nodiscard]] CRUCIBLE_INLINE ::foundation::SoleWriterProof<ProducerClaim, Role>
    prove_sole_writer(ExecCtx<ctx_cap::BrandedFg<Brand>, Row<>> const& /*context*/) const noexcept {
        return ::foundation::SoleWriterProof<ProducerClaim, Role>{};
    }

    using BrandRecord = detail::producer_claim::BrandRecord;
    using BrandRegistry = detail::producer_claim::BrandRegistry;

    // The key of the brand in the process-wide registry.  A brand needs a
    // stable identity, because the key must be the same in every shared
    // library: stable_type_id refuses a brand with internal linkage or with
    // no declared name, and it names the reason.  The first use of the key
    // evaluates the initializer one time, so a refused brand gives one error.
    static constexpr std::uint64_t brand_key_ = detail::producer_claim::brand_key_of(^^Brand);

    // The thread that holds the claim, or 0.  A relaxed load: each caller
    // only compares it with its own identity, and a thread reads its own
    // store in order.
    [[nodiscard]] CRUCIBLE_INLINE std::uintptr_t holder_now_() const noexcept {
        return holder_.load(std::memory_order_relaxed);
    }

    // Frees the holder with a release store, so each write of the thread that
    // held the claim happens before each access of the next holder, whose
    // exchange acquires.
    CRUCIBLE_INLINE void free_holder_() noexcept { holder_.store(0, std::memory_order_release); }

    [[gnu::cold, gnu::noinline]] void claim_or_reject_(std::uintptr_t current_tid) noexcept {
        std::uintptr_t holder = holder_now_();
        if (holder == 0) {
            // The exchange acquires the release store of release_claim, so
            // the writes of the thread that gave up the claim happen before
            // each access of this thread.  A failed exchange leaves the
            // winner's id in `holder`, which the check below reports.
            if (holder_.compare_exchange_strong(holder, current_tid, std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
                BrandRecord* const record = enter_brand_(current_tid);
                if (record != nullptr) {
                    entered_record_ = record;
                    return;
                }
                // Another thread holds a live claim of this brand.  The claim
                // is undone first, so that its destructor removes no entry
                // that it did not make.
                free_holder_();
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
            if (brand == brand_key_ || brand == 0) {
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
        record->brand.store(brand_key_, std::memory_order_release);
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
            if (brand == brand_key_) return &candidate;
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

// The context of a compute kernel: the brand of its thread, the FP mode of
// the register of that thread, and the empty row.  Each door gate refuses
// it, so a kernel allocates nothing, does no IO, does not block, and reads
// no clock, no merge and no peer.  Two modes are two types.
template <class Brand, ::foundation::core::FpMode Mode>
using KernelCtx = ExecCtx<ctx_cap::KernelSource<Brand, Mode>, Row<>>;

// The one door of a kernel context.  The context keeps the epoch of the
// scope and the thread of the caller, so the mode in its type is the mode
// of the register of that thread while the scope lives.
template <class Brand, ::foundation::core::FpMode Mode>
    requires ::foundation::os::EncodableFpMode<Mode>
[[nodiscard]] auto mint_kernel_context(::foundation::os::FpScope<Mode> const& scope) noexcept
    -> ExecCtx<ctx_cap::KernelSource<Brand, Mode>, Row<>> {
    return ExecCtx<ctx_cap::KernelSource<Brand, Mode>, Row<>>{scope.epoch(),
                                                              detail::producer_claim::calling_thread_identity()};
}

// A temporary scope ends at the end of the full expression of the mint, and
// the register then no longer holds the mode of the type.
template <class Brand, ::foundation::core::FpMode Mode>
    requires ::foundation::os::EncodableFpMode<Mode>
auto mint_kernel_context(::foundation::os::FpScope<Mode> const&&) noexcept
    -> ExecCtx<ctx_cap::KernelSource<Brand, Mode>, Row<>> = delete("a temporary scope ends at the end of the full "
                                                                   "expression, and the register then no longer "
                                                                   "holds the mode of the type");

namespace host {

// The run-time half of a kernel context, in each build.  A function can
// return a context by prvalue while its scope ends, and a body can take a
// reference to a context to another thread.  So the check reads that the
// calling thread is the thread of the mint, and that the scope of the
// context is the innermost FP scope of that thread.  The epoch is unique in
// the process, so the second check also proves that the scope lives and
// that no inner scope of another mode is open.  A raw write of the register
// under a live scope leaves the epoch as it was, so the third check reads the
// mode of the register itself.
template <class Brand, ::foundation::core::FpMode Mode>
void require_kernel_scope(KernelCtx<Brand, Mode> const& ctx) noexcept {
    CRUCIBLE_FATAL_INVARIANT(ctx.cap().thread() == detail::producer_claim::calling_thread_identity());
    CRUCIBLE_FATAL_INVARIANT(::foundation::os::is_innermost_fp_scope(ctx.cap().fp_scope_epoch()));
    CRUCIBLE_FATAL_INVARIANT(::foundation::os::FpControlWord::read().mode() == Mode);
}

}  // namespace host

template <class Brand, ::foundation::core::FpMode Mode>
class KernelCall;

// A kernel of the context KernelCtx<Brand, Mode>: a callable that takes the
// token of one call first and its buffers after it, and returns nothing.
template <class Kernel, class Brand, ::foundation::core::FpMode Mode, class... Args>
concept IsKernelOf = std::is_nothrow_invocable_r_v<void, Kernel&, KernelCall<Brand, Mode> const&, Args&&...>
                  && std::is_void_v<std::invoke_result_t<Kernel&, KernelCall<Brand, Mode> const&, Args&&...>>;

// The one door of a token, declared here so that the token can name it as a
// friend.  The definition is below the token.
template <class Brand, ::foundation::core::FpMode Mode, class Kernel, class... Args>
    requires IsKernelOf<Kernel, Brand, Mode, Args...>
void run_kernel(KernelCtx<Brand, Mode> const& ctx, Kernel&& kernel, Args&&... args) noexcept;

// The token of one call of a kernel.  run_kernel makes one after the check of
// the context (host::require_kernel_scope), and gives it to the kernel by
// reference.  Only run_kernel builds a token.  So a direct call of a kernel
// has no token and does not compile, and no call skips the check.  The token
// has no copy and no move, so it does not live past its call.  The kernel
// reaches its context through the token.
template <class Brand, ::foundation::core::FpMode Mode>
class[[= ::foundation::lifetime::no_start_over_bytes{}]] KernelCall {
public:
    using brand_type = Brand;
    static constexpr ::foundation::core::FpMode fp_mode = Mode;

    KernelCall(const KernelCall&) = delete("a token names one call of run_kernel, and a copy can live past the call");
    KernelCall(KernelCall&&) = delete("a token names one call of run_kernel, and a move can live past the call");
    KernelCall& operator=(const KernelCall&) = delete("a token names one call of run_kernel");
    KernelCall& operator=(KernelCall&&) = delete("a token names one call of run_kernel");
    constexpr ~KernelCall() noexcept {}

    // The context of the call, which run_kernel checked.
    [[nodiscard]] constexpr KernelCtx<Brand, Mode> const& ctx() const noexcept { return *ctx_; }

private:
    explicit constexpr KernelCall(KernelCtx<Brand, Mode> const& ctx) noexcept : ctx_{&ctx} {}

    template <class FriendBrand, ::foundation::core::FpMode FriendMode, class Kernel, class... Args>
        requires IsKernelOf<Kernel, FriendBrand, FriendMode, Args...>
    friend void run_kernel(KernelCtx<FriendBrand, FriendMode> const& ctx, Kernel&& kernel, Args&&... args) noexcept;

    KernelCtx<Brand, Mode> const* ctx_ = nullptr;
};

// Runs one kernel under its context, after the check of the context
// (host::require_kernel_scope).  The kernel takes the token of the call
// first, and its buffers after it.  It writes its results into its output
// buffers and returns nothing.  The check is one read of the thread pointer,
// one read of a thread-local slot, one read of the FP control register and
// three compares for each kernel, not for each element.
template <class Brand, ::foundation::core::FpMode Mode, class Kernel, class... Args>
    requires IsKernelOf<Kernel, Brand, Mode, Args...>
void run_kernel(KernelCtx<Brand, Mode> const& ctx, Kernel&& kernel, Args&&... args) noexcept {
    host::require_kernel_scope(ctx);
    KernelCall<Brand, Mode> const call{ctx};
    kernel(call, static_cast<Args&&>(args)...);
}

// The test door of the foreground context.  The rule owners of
// the quarantine plugin refuses a use of it in code that ships, as it does
// for the door in Effect.h.
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

// The row holds E, and the source permits each atom of the row.  Only a
// context permits a value atom, and a context holds each value atom that it
// permits as a field.  So the source has the field of E, and the door copies
// it.  The copy of an atom is an empty body, so the door costs no
// instruction in each build.
template <Effect E, IsExecCtx Ctx>
    requires CtxOwnsCapability<Ctx, E> && HasBareTag<E>
[[nodiscard]] constexpr bare_tag_t<E> extract_bare_from_ctx(Ctx const& ctx) noexcept {
    if constexpr (E == Effect::Alloc) {
        return ctx.cap_.alloc;
    } else if constexpr (E == Effect::IO) {
        return ctx.cap_.io;
    } else if constexpr (E == Effect::Block) {
        return ctx.cap_.block;
    } else {
        static_assert(E == Effect::Nondet, "a value atom with a bare tag needs its arm in extract_bare_from_ctx");
        return ctx.cap_.nondet;
    }
}

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
using BgBlockWitness =
    ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block, Effect::Nondet, Effect::Div>>;
using BgNondetWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Nondet>>;
using InitWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using InitBlockWitness =
    ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block, Effect::Nondet, Effect::Div>>;
using TestWitnessCtx =
    ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block, Effect::Nondet, Effect::Div>>;

// The eight named contexts.  Each has two axes, the capability source
// and the row.  Each is one witness above.  The check file of this
// header states the row of each, so the layer that promotes them to
// production contexts starts from a declaration.

// The context of the foreground thread that runs dispatch.
using HotFgCtx = FgWitness;

using BgDrainCtx = BgWitness;

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = BgIoWitness;

// The load context claims Block and Nondet on top of the compile row.
// Work that enters the kernel and waits there, such as a BPF program load
// waiting on the verifier, needs Block, and a wait to a deadline reads a
// clock.  The background capability permits these five atoms, so this is
// the widest row a background context can claim.
using BgLoadCtx = BgBlockWitness;

// The observe context claims Nondet on top of the atom of the thread, and
// nothing else.  A watchdog of a background thread reads a clock and
// compares it with a deadline.  It allocates nothing and does no IO.
using BgObserveCtx = BgNondetWitness;

// The context of process startup, before the threads are pinned.
using ColdInitCtx = InitWitness;

// The startup load context claims Block and Nondet on top of the cold
// init row.  Startup work that waits in the kernel, such as a BPF program
// load that waits for the verifier, needs Block, and a calibration reads a
// clock.  The init capability permits these five atoms, so this is the
// widest row an init context can claim.
using InitLoadCtx = InitBlockWitness;

// A fixture may claim any effect this row names, and no others.  It holds
// Nondet, so a test can read a clock.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = TestWitnessCtx;

}  // namespace detail::ctx_witnesses

}  // namespace foundation::effects
