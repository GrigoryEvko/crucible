#pragma once

// ── crucible::handles umbrella ──────────────────────────────────────
//
// RAII resource handles + first-call-wins publication primitives:
//   FileHandle              — POSIX fd, std::expected<T, SyscallError>
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
