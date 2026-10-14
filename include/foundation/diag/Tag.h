#pragma once

// The vocabulary of a diagnostic tag: the base class of each tag, the type
// of a text field, the severity, the closed set of the categories and the
// readers that take one tag type.  A header that names a category, derives a
// tag or reads the fields of one tag includes this header and no prose.  The
// tags of the catalog are in the domain files of foundation/diag/tags/, and
// foundation/diag/Catalog.h puts them together into the closed set.

#include <foundation/Platform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::diag {

struct tag_base {
    constexpr tag_base() noexcept = default;
    constexpr tag_base(const tag_base&) noexcept = default;
    constexpr tag_base(tag_base&&) noexcept = default;
    constexpr tag_base& operator=(const tag_base&) noexcept = default;
    constexpr tag_base& operator=(tag_base&&) noexcept = default;
    ~tag_base() = default;
};

// The text of one field of a tag: a std::string_view over its string
// literal.  A std::string_view built from a pointer counts the characters
// one at a time in a constant evaluation.  Each translation unit that
// includes this header would do that count again for each field of each
// tag, and the fields hold about 100,000 characters.  GCC folds
// __builtin_strlen of a string literal to a constant in one step.  The
// constructor is not a template, so the fields of different lengths share
// one instantiation.
struct tag_text_literal : std::string_view {
    consteval tag_text_literal(const char (&literal)[]) noexcept
        : std::string_view{literal, __builtin_strlen(literal)} {}
};

// Error and Fatal behave identically at compile time. They differ in
// what tooling is meant to do: Fatal marks a violation whose silent
// passage is dangerous rather than merely wrong, and offers no override.
//
// The severity grades a tag, so it is declared beside the tags rather
// than beside the reader in Insights.h.  Insights.h includes this header.
enum class Severity : std::uint8_t {
    Hint = 0,  // the code is correct and could be better shaped
    Warning = 1,  // the code compiles and carries a runtime risk
    Error = 2,  // the assertion or the implementation has to change
    Fatal = 3,  // as Error, and no override path
};

// Authoring rule for every tag of the domain files: the description states
// what the bug class is, the remediation states how to fix an instance of
// it. Both read as standalone sentences, because a build log shows them
// without the file of the tag.
//
// A tag also carries the prose a structured rejection prints: a
// severity, why the rule exists, how the violation usually arrives, and
// the compliant line beside the violating one. The tag carries them as
// members, and Insights.h reads the members off whichever tag it is
// asked about.
//
// When writing them: the why states the architectural constraint and
// answers what actually breaks if it is ignored. The symptom describes
// how the violation typically arrives, so a reader can match it against
// the change they just made. Both examples are one line of real C++,
// and they differ only in the thing the diagnostic is about. A tag that
// declares none of them still compiles, and prints the shorter block.

template <typename T>
inline constexpr bool is_diagnostic_class_v = std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

namespace detail {

// One selector per text field.  Each reads its member directly, so a tag
// that declares no such member is a compile error at the read.  The
// catalog arrays of foundation/diag/Catalog.h vary over these three, and
// their walk is written once.
inline constexpr auto select_name = []<typename Tag>() consteval { return std::string_view{Tag::name}; };
inline constexpr auto select_description = []<typename Tag>() consteval { return std::string_view{Tag::description}; };
inline constexpr auto select_remediation = []<typename Tag>() consteval { return std::string_view{Tag::remediation}; };

// The direct form constrains the variable template itself with
// `requires is_diagnostic_class_v<T>`. A requires-clause failure on a variable
// template reports in compiler-chosen wording, which drifts between releases.
// Routing through this function puts the wording under our control.  The
// if constexpr keeps the read away from a type that declares no name.
template <typename T>
[[nodiscard]] consteval std::string_view tag_name_() noexcept {
    static_assert(is_diagnostic_class_v<T>, "foundation::diag [DiagnosticAccessor_NonTag]: "
                                            "diagnostic_name_v requires T to be derived from "
                                            "foundation::diag::tag_base.  See foundation/diag/Catalog.h's catalog "
                                            "for the shipped tag classes; user-extensions inherit "
                                            "tag_base and provide constexpr name/description/remediation.");
    if constexpr (is_diagnostic_class_v<T>) {
        return select_name.template operator()<T>();
    } else {
        return {};
    }
}

}  // namespace detail

template <typename T>
inline constexpr std::string_view diagnostic_name_v = detail::tag_name_<T>();

template <typename DiagnosticClass, typename... Context>
    requires is_diagnostic_class_v<DiagnosticClass>
struct Diagnostic {
    using diagnostic_class = DiagnosticClass;
    using context = std::tuple<Context...>;

    static constexpr std::string_view name = DiagnosticClass::name;
    static constexpr std::string_view description = DiagnosticClass::description;
    static constexpr std::string_view remediation = DiagnosticClass::remediation;
};

template <typename T>
struct is_diagnostic : std::false_type {};

template <typename C, typename... Ctx>
struct is_diagnostic<Diagnostic<C, Ctx...>> : std::true_type {};

template <typename T>
inline constexpr bool is_diagnostic_v = is_diagnostic<T>::value;

// Append-only. A new tag is a struct in a domain file of
// foundation/diag/tags/, and a Category enumerator appended here at the next
// integer value; the tuple of foundation/diag/Catalog.h is derived from this
// enum and is not written by hand. The integer values are the ordinal pins:
// reordering or renumbering existing enumerators changes the indices that
// federation cache keys are built from, which invalidates every stored key.
enum class Category : std::uint8_t {
    EffectRowMismatch = 0,
    UnknownParameterShape = 1,
    GradedWrapperViolation = 2,
    LinearityViolation = 3,
    RefinementViolation = 4,
    HotPathViolation = 5,
    DetSafeLeak = 6,
    NumericalTierMismatch = 7,
    MemOrderViolation = 8,
    AllocClassViolation = 9,
    VendorBackendMismatch = 10,
    CrashClassMismatch = 11,
    ConsistencyMismatch = 12,
    LifetimeViolation = 13,
    WaitStrategyViolation = 14,
    ProgressClassViolation = 15,
    CipherTierViolation = 16,
    ResidencyHeatViolation = 17,
    EpochMismatch = 18,
    BudgetExceeded = 19,
    NumaPlacementMismatch = 20,
    RecipeSpecMismatch = 21,
    PureFunctionViolation = 22,
    DivergenceBudgetViolation = 23,
    StateBudgetViolation = 24,
    InsufficientWitness = 25,
    ModalityMismatch = 26,
    LinearAliasViolation = 27,
    SharedPermissionPoolSaturated = 28,
    HugePageAllocationFailed = 29,
    PublishOnceDoublePublish = 30,
    BitsInvariantViolation = 31,
    BorrowedBoundsViolation = 32,
    AllocationSizeOverflow = 33,
};

// The count of the enumerators, which is the count of the tags.  It is a
// literal, so that no includer walks the enum.  The check file of
// foundation/diag/Catalog.h derives the count and pins the literal to it.
inline constexpr std::size_t catalog_size = 34;

namespace detail {

template <std::size_t... Is>
[[nodiscard]] consteval auto categories_array_impl(std::index_sequence<Is...>) noexcept
    -> std::array<Category, sizeof...(Is)> {
    return std::array<Category, sizeof...(Is)>{static_cast<Category>(Is)...};
}

}  // namespace detail

inline constexpr auto categories_v = detail::categories_array_impl(std::make_index_sequence<catalog_size>{});

namespace detail {

template <typename F, std::size_t... Is>
constexpr void enumerate_categories_impl(F&& f, std::index_sequence<Is...>) noexcept {
    (f.template operator()<static_cast<Category>(Is)>(), ...);
}

}  // namespace detail

template <typename F>
constexpr void enumerate_categories(F&& f) noexcept {
    detail::enumerate_categories_impl(std::forward<F>(f), std::make_index_sequence<catalog_size>{});
}

template <typename Tag, typename... Args>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto mint_diagnostic(Args&&...) noexcept -> Diagnostic<Tag, std::remove_cvref_t<Args>...> {
    return Diagnostic<Tag, std::remove_cvref_t<Args>...>{};
}

}  // namespace foundation::diag

// A condition holding a comma, such as a template argument list, must be
// parenthesised whole, or the preprocessor splits it across the parameters.
#define CRUCIBLE_DIAG_ASSERT(cond, tag, msg) static_assert(cond, "foundation::diag [" #tag "]: " msg)
