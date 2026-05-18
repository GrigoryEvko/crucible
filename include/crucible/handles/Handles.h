#pragma once

// ── crucible::handles umbrella ──────────────────────────────────────
//
// RAII resource handles + first-call-wins publication primitives:
//   Fd                      — strong-typed POSIX fd (fixy-A1-012)
//   FileHandle              — move-only RAII fd; factories return
//                             std::expected<FileHandle, std::error_code>
//                             (fixy-A1-013); read_full / write_full /
//                             file_size also return std::expected<...,
//                             std::error_code> (fixy-A1-030) — the
//                             legacy negative-encodes-errno overload is
//                             retired, removing the §X TypeSafe gap
//                             where a real EOF read aliased with an
//                             errno-shaped negative sentinel.
//   Once / Lazy<T>          — first-call-wins single init
//   OneShotFlag             — single-bit release/acquire publication
//   PublishOnce<T>          — one-shot async channel-resource publication
//   PublishSlot<T>          — reusable latest-wins pointer publication
//   LazyEstablishedChannel  — PublishOnce-backed session establishment
//
// Hot-path TUs that only need a single primitive should include the
// targeted header directly to minimize compile cost.

#include <crucible/handles/FileHandle.h>
#include <crucible/handles/LazyEstablishedChannel.h>
#include <crucible/handles/Once.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/handles/PublishOnce.h>
