// GCC 16 miscompile: bad codegen for do_not_optimize(T&) applied to a
// non-const pointer lvalue inside a lambda captured by reference, when
// the pointed-to type's hot-inlined method has a pre() contract.
//
// Reproducer for gcc-bugzilla. Compiles with GCC 16, crashes at -O3.
//
// Build:
//   g++-16 -std=c++26 -fcontracts -O3 -march=native bug.cpp handler.cpp -o bug
// Run:
//   ./bug         # SIGSEGV
//
// Removing any one of the following makes the crash disappear:
//   - the `pre(sid < num_slots_)` contract on Pool::slot_ptr
//   - the `do_not_optimize(T&)` overload (leaving only T const&)
//   - the `do_not_optimize(pool_ptr)` call before bench::run
//   - the preceding bench::run call (needs 2 Runs, not 1)
//   - `-fcontracts` (no -fcontracts = no crash)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// ── Pool: hot-inlined slot_ptr with a pre() contract ─────────────────
struct Pool {
    Pool() = default;
    ~Pool() { std::free(ptr_table_); }
    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&)                 = delete;
    Pool& operator=(Pool&&)      = delete;

    [[gnu::cold, gnu::noinline]]
    void init(uint32_t n) noexcept
        pre (ptr_table_ == nullptr)
    {
        num_slots_ = n;
        ptr_table_ = static_cast<void**>(std::calloc(n, sizeof(void*)));
        if (!ptr_table_) std::abort();
        for (uint32_t i = 0; i < n; ++i)
            ptr_table_[i] = reinterpret_cast<void*>(uintptr_t(0x1000) + i * 8);
    }

    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
    inline void* slot_ptr(uint32_t sid) const noexcept
        pre (sid < num_slots_)            // ← contract is part of the trigger
    {
        return ptr_table_[sid];
    }

private:
    void**   ptr_table_ = nullptr;
    uint32_t num_slots_ = 0;
    uint32_t pad_       = 0;
};

// ── Minimal bench harness ────────────────────────────────────────────
namespace bench {

// The Google-Benchmark-style DoNotOptimize pair. The T& overload with
// "+m,r" is what triggers the bug when applied to a non-const pointer
// lvalue. T const& is safe.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& v) noexcept {
    asm volatile("" : : "r,m"(v) : "memory");
}
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T& v) noexcept {
    asm volatile("" : "+m,r"(v) : : "memory");            // ← trigger
}

struct Report {
    std::string name;
};

class Run {
public:
    explicit Run(std::string name) : name_{std::move(name)} {}

    template <typename Body>
    [[nodiscard]] Report measure(Body&& body) const {
        const size_t batch = auto_batch_(body);    // ← must be a separate templated fn
        for (size_t i = 0; i < batch; ++i) body(); // ← must call body in measure()
        return Report{name_};
    }

private:
    template <typename Body>
    [[nodiscard]] size_t auto_batch_(Body&& body) const {
        body();                                    // ← must call body here too
        return 1;
    }

    std::string name_;
};

template <typename Body>
[[nodiscard]] inline Report run(std::string name, Body&& body) {
    return Run{std::move(name)}.measure(std::forward<Body>(body));
}

} // namespace bench

// ── main ─────────────────────────────────────────────────────────────
int main() {
    std::vector<bench::Report> reports;

    // PRECEDING Run — its presence primes something in the optimizer
    // so the next Run miscompiles. With just the second Run alone, no
    // crash. With any call to slot_ptr() before the second Run's
    // lambda, crash.
    reports.push_back([]{
        Pool pool; pool.init(1);
        return bench::run("first", [&]{
            bench::do_not_optimize(pool.slot_ptr(0));
        });
    }());

    // MISCOMPILED Run.
    reports.push_back([]{
        constexpr uint32_t N = 100;
        Pool pool; pool.init(N);
        const Pool* pool_ptr = &pool;
        bench::do_not_optimize(pool_ptr);              // ← trigger
        return bench::run("second", [&]{
            for (uint32_t i = 0; i < N; ++i)
                bench::do_not_optimize(pool_ptr->slot_ptr(i));   // SIGSEGV
        });
    }());

    std::printf("%zu reports OK\n", reports.size());
    return 0;
}
