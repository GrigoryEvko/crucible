// SPDX-License-Identifier: Apache-2.0
// crucible — safety/Contract.h
//
// Umbrella header for the consteval-aware contract macros.  Single
// include for adoption sites that want both `CRUCIBLE_PRE` and
// `CRUCIBLE_POST` without two `#include` lines.
//
// Per misc/08_05_2026_harness.md §4: the macro pair forms the Tier-0
// VC harness, the consteval-fire counterpart to native P2900R14
// `pre()` / `post()` clauses.  See Pre.h and Post.h for the rationale,
// usage patterns, cost model, and axiom analysis.
//
// Use as:
//   #include <crucible/safety/Contract.h>
//   constexpr T fn(StructType const& s) noexcept {
//       CRUCIBLE_PRE(invariant(s));
//       T r = compute(s);
//       CRUCIBLE_POST(r, post_invariant(r));
//       return r;
//   }

#pragma once

#include <crucible/safety/Pre.h>
#include <crucible/safety/Post.h>
