// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// OrderedAppendOnly<T, KeyFn, Cmp>::append this->-member CRUCIBLE_PRE
// — backward-key rejection on disjunction predicate
// `inner_.empty() || !Cmp{}(KeyFn{}(item), KeyFn{}(inner_.back()))`.
//
// Distinct mismatch class: the predicate is a DISJUNCTION of two
// member-on-member subexpressions PLUS stateless-functor calls
// (KeyFn{}, Cmp{}).  The most complex predicate shape in the file —
// short-circuited boolean on `this->inner_` member chain.  Vanilla
// P2900 pre() bypasses at consteval for foldable append() bodies;
// the body CRUCIBLE_PRE form fires.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — backward-key append trips the disjunction's
// right operand → CRUCIBLE_PRE fires at consteval.

#include <crucible/safety/Mutation.h>
#include <cstdint>

namespace {

// Note: OrderedAppendOnly defaults Storage to AppendOnly<T> which in
// turn uses std::vector — not consteval-friendly.  Instead, test the
// CRUCIBLE_PRE through a runtime path that still witnesses the trap
// macro fires consistently across foldable bodies.  The migration's
// load-bearing claim is clause-shape uniformity; consteval-fire
// coverage is provided by the SetOnce/WriteOnce/Monotonic fixtures
// for the same family of predicate shapes.  This file is the
// runtime-side witness for the disjunction-on-member predicate
// class.
//
// We use a constexpr-eligible test via a custom Storage stub whose
// container is std::array, so the wrapper itself is consteval-fit.

template <typename T>
struct ArrayStorage {
    T data[8]{};
    std::size_t n = 0;
    using value_type     = T;
    using const_iterator = const T*;

    constexpr ArrayStorage() = default;
    constexpr void append(T item) noexcept { data[n++] = item; }
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return data[i]; }
    [[nodiscard]] constexpr const T& front() const noexcept { return data[0]; }
    [[nodiscard]] constexpr const T& back()  const noexcept { return data[n - 1]; }
    [[nodiscard]] constexpr std::size_t size()  const noexcept { return n; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return n == 0; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data; }
    [[nodiscard]] constexpr const_iterator end()   const noexcept { return data + n; }
    [[nodiscard]] constexpr ArrayStorage<T> drain() && noexcept { return std::move(*this); }
};

struct IdentityKey {
    constexpr uint32_t operator()(uint32_t v) const noexcept { return v; }
};

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::OrderedAppendOnly<uint32_t, IdentityKey, std::less<uint32_t>, ArrayStorage> o{};
    o.append(10u);
    o.append(5u);  // CRUCIBLE_PRE disjunction MUST fire (key 5 < 10)
    return 0;
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on OrderedAppendOnly::append's disjunction-with-"
    "member-function-on-member predicate MUST fire at consteval when "
    "a backward key is appended.  If this static_assert evaluates "
    "successfully, the body-CRUCIBLE_PRE migration failed for the "
    "most-complex predicate shape in Mutation.h.");

}  // namespace

int main() { return 0; }
