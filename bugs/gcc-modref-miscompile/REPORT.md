# GCC 16 miscompile: wrong extra indirection when inlining a hot
# method called through a reference-captured pointer that was passed
# to `asm volatile("" : "+m,r"(ptr) : : "memory")`

## Summary

GCC 16 emits an extra `mov (%rax),%rax` load when inlining a class
method reached through a pointer that:

1. Is passed to an asm with `"+m,r"(ptr)` constraint + `"memory"`
   clobber (the canonical Google-Benchmark-style `DoNotOptimize(T&)`
   idiom),
2. Is captured by reference in a lambda,
3. The lambda body calls a hot-inlined method on the pointed-to object
   (here: `pool_ptr->slot_ptr(sid)` which returns `ptr_table_[sid]`).

Instead of emitting:
```
mov 0x8(%rdx),%rax     # rax = this->ptr_table_  (offset 8)
mov (%rax,sid,8),%rax  # ptr_table_[sid]
```

GCC 16 emits:
```
mov (%rdx),%rdx        # ← bogus extra indirection
mov 0x8(%rdx),%rdx     # SIGSEGV — reads offset 8 from the bogus value
```

The miscompile manifests as SIGSEGV/SIGBUS on the contract check
(when `pre()` reads `num_slots_`) or on the payload load itself.

## Two flavors

### Flavor A — narrow, minimal, captured in bug.cpp

- Requires: `-O3 -fcontracts` with default `enforce` or `observe`
  contract semantic
- Fixed by: `-fno-ipa-modref` OR `-fno-early-inlining`
- Fixed by: `-fcontract-evaluation-semantic=ignore`
- Fixed by: replacing `do_not_optimize(T&)` with `do_not_optimize(T const&)`
- Fixed by: removing the `pre(...)` contract from the hot method

Reproducer: **`bug.cpp`** (135 lines) + **`handler.cpp`** (14 lines).

```
g++-16 -std=c++26 -fcontracts -O3 -march=native bug.cpp handler.cpp -o bug
./bug       # SIGBUS/SIGSEGV
```

### Flavor B — real production bench, harder to minimize

Same crash shape, same source-level pattern, different sensitivity:

- Requires `-O1` or higher (crashes at -O1/-O2/-O3/-Os)
- NOT fixed by `-fno-ipa-modref`
- NOT fixed by `-fno-early-inlining`
- NOT fixed by `-fcontract-evaluation-semantic=ignore`
- ONLY fixed by `-fno-inline` (which kills all inlining, not viable
  in production)
- Fixed at source level by the same "don't clobber the pointer itself"
  pattern

This flavor appears in a larger benchmark suite that we haven't been
able to reduce to a standalone file yet. Same crash shape (three-mov
indirection chain in the inlined hot method), same trigger family
(`+m,r`-clobbered typed pointer captured by lambda), different
optimization stage. Likely the same underlying miscompile manifesting
through a different code path.

## Disassembly from Flavor A at -O3

```asm
0x0000000000400486 <+0>:   push   %r12
0x0000000000400488 <+2>:   mov    %rdi,%r12       # r12 = closure
0x000000000040048b <+5>:   push   %rbp
0x000000000040048c <+6>:   push   %rbx
0x000000000040048d <+7>:   xor    %ebx,%ebx       # sid = 0
0x000000000040048f <+9>:   mov    (%r12),%rax     # rax = closure[0] = &pool_ptr
0x0000000000400493 <+13>:  mov    (%rax),%rbp     # rbp = *&pool_ptr = &pool
=> 0x400496 <+16>:         cmp    0x8(%rbp),%ebx  # ← contract: sid < num_slots_
```

Should be: the contract reads `pool.num_slots_` at offset 24 in Pool,
not offset 8.  Offset 8 of Pool is `ptr_table_` (a pointer). The cmp
treats a heap pointer's low 32 bits as an integer count.

## Disassembly from Flavor B at -O1 (real bench)

```asm
0x406ffe:  mov    (%rbx),%rdx      # rdx = closure[0] = &pool_ptr
0x407001:  mov    (%rdx),%rdx      # rdx = *&pool_ptr = pool_ptr = &pool
=> 0x407004: mov  0x8(%rdx),%rdx   # ← SIGSEGV
```

Three loads, same extra-indirection shape. This one should succeed
(offset 8 of a valid stack Pool is `ptr_table_`) but it doesn't —
suggesting `rdx` after the second mov isn't actually `&pool`. Needs
further reduction to tell the exact miscompile.

## Toolchain

```
$ g++ --version
g++ (GCC) 16.0.1 20260416 (Red Hat 16.0.1-0)
```

Fedora rawhide. GCC upstream-trunk snapshotted 2026-04-16.

## Our in-codebase mitigation

```cpp
// BROKEN — triggers miscompile
const Pool* pool_ptr = &pool;
bench::do_not_optimize(pool_ptr);           // ← culprit
bench::run("", [&]{
    bench::do_not_optimize(pool_ptr->slot_ptr(i));
});

// WORKING — only clobber the method's return value
const Pool* pool_ptr = &pool;
bench::run("", [&]{
    bench::do_not_optimize(pool_ptr->slot_ptr(i));  // only this
});
```

## Files in this directory

- `bug.cpp` — Flavor A self-contained reproducer
- `handler.cpp` — libstdc++ requires this when `-fcontracts` is on
- `REPORT.md` — this file

## How we found it

Production benchmarking code triggered SIGSEGV when we applied
`bench::do_not_optimize(pool_ptr)` before handing `pool_ptr` into a
lambda body. Removing the clobber fixed it. Reducing the code to
understand why surfaced Flavor A cleanly. Flavor B remains in the
real production file; crash shape and source pattern are identical,
but we haven't yet isolated which pass is responsible.
