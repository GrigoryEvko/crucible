# Phase 6+ Deferred Decisions

This records the GAPS-109 decisions for `misc/03_05_2026.md` sections 8.4,
8.5, and 8.6. These decisions do not block the current GAPS queue. They are
deferred because the single-tenant distributed substrate, `rt/` observation
rail, Cipher federation path, and cache-key discipline need to stay simple
until their production shape is proven.

## 8.4 Live Vigil Evolution

Decision: defer to Phase 7+.

Running Vigil structure changes are not part of the current runtime contract.
The accepted near-term model is restart or iteration-boundary replacement for
structural changes, with existing DAG branch and atomic activation machinery
handling compiled-region swaps. Live insertion of stages, live capability
requirement changes, or live model-architecture mutation would require a
cross-layer invariant spanning `Vigil`, `Cipher`, `Session`, `Machine`,
permission ownership, and replay. That invariant is not needed for the current
single-vigil substrate work.

Trigger to reopen: a production workflow needs structure-changing evolution
without restart, and the request names the exact transition class that must be
live, such as adding a stage, changing a session protocol, changing a resource
row, or migrating a running branch across topology epochs. The follow-up task
must specify the activation boundary, rollback path, replay semantics, and the
permission/session state that survives the transition.

## 8.5 Multi-Tenancy

Decision: defer until the single-tenant distributed runtime is proven.

Multi-tenancy is not a small flag on top of the current design. It introduces
tenant identity, per-tenant capability quotas, Cipher-tier ownership, cross-
tenant data-flow denial, cache-sharing policy, operator audit boundaries, and
resource-admission failure modes. Shipping those concepts before single-tenant
Canopy/CNTP/Cipher federation is mature would add policy surfaces without a
stable substrate to enforce them.

Trigger to reopen: a concrete deployment needs two or more independent tenants
on the same fleet. The follow-up task must define tenant identity, capability
quota carriers, Cipher namespace isolation, cache-sharing rules, and the
compile-time or runtime admission point that rejects cross-tenant flow.

## 8.6 Cross-Version Reproducibility

Decision: defer to Phase 9 federation work.

Current cache and serialization paths intentionally use strict version equality:
CDAG format version mismatches fail, federation protocol version mismatches
fail, and cache keys are treated as exact structural identity. That is the
right behavior while the IR and compiler are still moving. A compatibility
range such as `compiler_version_compat` is only sound once IR001 canonical
forms, compiler version identity, and accepted compatibility relations are
specified. Until then, silent cross-version reuse is more dangerous than a
cache miss.

Trigger to reopen: cross-organization federation or long-lived cache sharing
requires reuse across compiler versions. The follow-up task must add a typed
compiler-build identity, an explicit compatibility range to the relevant Cipher
or federation cache entry, negative tests for incompatible reuse, and positive
tests proving compatible reuse does not bypass content, row, protocol, or
format-version checks.
