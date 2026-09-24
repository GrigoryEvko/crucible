// SPDX-License-Identifier: Apache-2.0
//
// A native P2900 pre() clause is silently skipped at consteval for most
// parameter shapes, and fires for one. The nine near-identical functions below
// exist because the macro pair has to cover every one of those shapes, not
// just the shape that happens to work natively.
//
// The file is silent on success and exits zero. A violated contract aborts,
// which is how a failure surfaces. The violating direction cannot be written
// here at all and lives in compile-failure fixtures.

#include <crucible/safety/Contract.h>
#include <contracts>

#include <cstdint>
#include <cstdio>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

struct R {
    int v = 0;
};

// Shape 1: scalar by value.
[[nodiscard]] constexpr int pre_scalar(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    return x * 2;
}

// Shape 2: struct by const reference. This is the shape a native pre() clause
// skips, and the one most production signatures use.
[[nodiscard]] constexpr std::uint64_t pre_struct_cref(S const& s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 3: struct by const value.
[[nodiscard]] constexpr std::uint64_t pre_struct_cval(S const s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 4: struct by value.
[[nodiscard]] constexpr std::uint64_t pre_struct_val(S s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 5: struct through a const pointer.
[[nodiscard]] constexpr std::uint64_t pre_struct_cptr(S const* s) noexcept {
    CRUCIBLE_PRE(s != nullptr && s->nz());
    return s->lo;
}

// Shape 6: post on a named scalar result.
[[nodiscard]] constexpr int post_scalar(int const x) noexcept {
    int const r = x + 1;
    CRUCIBLE_POST(r, r > 0);
    return r;
}

// Shape 7: post on a field of a struct result.
[[nodiscard]] constexpr R post_struct_field(int const x) noexcept {
    R r{x + 1};
    CRUCIBLE_POST(r, r.v > 0);
    return r;
}

// Shape 8: both clauses in one function, which is the shape production code
// most often has.
[[nodiscard]] constexpr int pre_and_post(int x) noexcept {
    CRUCIBLE_PRE(x > 0 && x < 1000);
    int const r = x * 3;
    CRUCIBLE_POST(r, r >= x && r < 3000);
    return r;
}

// Shape 9: the macro used mid-body as an invariant rather than as a
// function-entry contract.
[[nodiscard]] constexpr int mid_body_assert(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    int y = x * 2;
    CRUCIBLE_PRE(y > x);
    int z = y + 1;
    CRUCIBLE_POST(z, z > y);
    return z;
}

// Every shape is evaluated inside a static_assert, which is the context the
// compile-failure fixtures depend on. If the consteval path breaks, those
// fixtures start passing for the wrong reason and stop guarding anything.

constexpr S OK_S{42};

static_assert(pre_scalar(5) == 10, "shape 1 positive");
static_assert(pre_struct_cref(OK_S) == 42, "shape 2 positive");
static_assert(pre_struct_cval(OK_S) == 42, "shape 3 positive");
static_assert(pre_struct_val(OK_S) == 42, "shape 4 positive");
static_assert(pre_struct_cptr(&OK_S) == 42, "shape 5 positive");
static_assert(post_scalar(5) == 6, "shape 6 positive");
static_assert(post_struct_field(5).v == 6, "shape 7 positive");
static_assert(pre_and_post(10) == 30, "shape 8 positive");
static_assert(mid_body_assert(5) == 11, "shape 9 positive");

// The postcondition leaves an assumption behind that a caller can exploit.
// Nothing here measures the generated code. The claim is only that the
// invariant survives the call boundary.

[[nodiscard]] constexpr int relies_on_post(int x) noexcept {
    int const r = post_scalar(x);
    // The callee's postcondition already rules this branch out, so it is here
    // to be elided rather than taken.
    if (r <= 0) [[unlikely]]
        return -1;
    return r;
}

static_assert(relies_on_post(5) == 6, "post hint propagation");

// The fast variant traps directly instead of routing through the violation
// handler. Its consteval branch is identical, so it shares the regular
// macro's compile-failure fixtures and needs only positive coverage here.

[[nodiscard]] constexpr int pre_fast_scalar(int x) noexcept {
    CRUCIBLE_PRE_FAST(x > 0);
    return x * 3;
}

[[nodiscard]] constexpr std::uint64_t pre_fast_struct(S const& s) noexcept {
    CRUCIBLE_PRE_FAST(s.nz());
    return s.lo;
}

static_assert(pre_fast_scalar(5) == 15, "PRE_FAST scalar positive");
static_assert(pre_fast_struct(OK_S) == 42, "PRE_FAST struct positive");

// The message-carrying forms, checked for the same shapes, so the extra
// argument cannot change whether the clause is reached.

[[nodiscard]] constexpr int pre_msg_scalar(int x) noexcept {
    CRUCIBLE_PRE_MSG(x > 0, "scalar input must be strictly positive");
    return x + 100;
}

[[nodiscard]] constexpr R post_msg_struct(int const x) noexcept {
    R r{x + 50};
    CRUCIBLE_POST_MSG(r, r.v > 0, "compute path must produce strictly positive R::v");
    return r;
}

[[nodiscard]] constexpr int post_fast_scalar(int const x) noexcept {
    int const r = x + 7;
    CRUCIBLE_POST_FAST(r, r > 0);
    return r;
}

static_assert(pre_msg_scalar(3) == 103, "PRE_MSG scalar positive");
static_assert(post_msg_struct(7).v == 57, "POST_MSG struct positive");
static_assert(post_fast_scalar(11) == 18, "POST_FAST scalar positive");

// The native clause, with an always-true predicate, so it pins that the
// native form still compiles and evaluates at consteval alongside the macros.
// Which way it fires on a violation is not observable from a passing
// assertion, and the macro fixtures already cover that direction for the
// equivalent shape.

[[nodiscard]] constexpr int native_contract_assert_witness(int x) noexcept {
    contract_assert(x > 0);
    return x;
}

static_assert(native_contract_assert_witness(7) == 7,
              "a native contract_assert with an always-true predicate must compile "
              "and evaluate cleanly at consteval.");

}  // namespace

// The assertions above cover the consteval path. This covers the other one:
// the same macros have to emit working runtime code outside NDEBUG. Every
// input below is known good, so reaching the end is the pass.

int main() {
    int volatile sink = 0;
    sink += pre_scalar(7);
    sink += static_cast<int>(pre_struct_cref(S{99}));
    sink += static_cast<int>(pre_struct_cval(S{100}));
    sink += static_cast<int>(pre_struct_val(S{101}));
    constexpr S const tmp{102};
    sink += static_cast<int>(pre_struct_cptr(&tmp));
    sink += post_scalar(13);
    sink += post_struct_field(14).v;
    sink += pre_and_post(20);
    sink += mid_body_assert(3);
    sink += relies_on_post(50);

    // On a violation the fast form traps directly and the native form routes
    // through the violation handler. Both abort, by different paths.
    sink += pre_fast_scalar(11);
    sink += static_cast<int>(pre_fast_struct(S{77}));
    sink += native_contract_assert_witness(13);

    sink += pre_msg_scalar(8);
    sink += post_msg_struct(3).v;
    sink += post_fast_scalar(15);

    // The sink is volatile so none of the calls above can be optimized away.
    if (sink == 0) {
        std::fprintf(stderr, "test_pre_post: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
