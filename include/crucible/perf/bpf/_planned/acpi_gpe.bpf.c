/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * acpi_gpe.bpf.c — ACPI GPE (General Purpose Event) firing observation.
 *
 * STATUS: doc-only stub.  DEPRIORITIZED — there is NO `acpi/*` tracepoint
 * subsystem in the kernel (verified against kernel 6.17 + mainline
 * include/trace/events/).  Observation requires kprobe on
 * `acpi_ev_gpe_dispatch` / `acpi_ns_evaluate` (drivers/acpi/acpica/).
 * Niche bare-metal-only signal; firmware GPE storms are easier to spot
 * via existing HardIrq facade's per-IRQ histogram (ACPI SCI shows up
 * there).  Defer until a fleet actually needs ACPI-attributed kernel time.
 *
 * ─── PROBLEM ──────────────────────────────────────────────────────────
 * ACPI GPEs are firmware-generated interrupts that the kernel ACPI
 * subsystem dispatches to ACPI methods (AML interpreter).  GPE storms
 * — buggy firmware, hardware error degraded mode, thermal events,
 * misconfigured power buttons, flaky USB controllers — manifest as
 * "the system spent 5% of CPU in ACPI handlers".  Manifests in our
 * bench as "all kernel time mysteriously moved to interrupt context".
 *
 * Per-GPE-number rate exposes WHICH firmware event is firing; per-
 * handler-method exposes WHICH AML routine is consuming CPU.
 *
 * ─── MECHANISM ────────────────────────────────────────────────────────
 * BPF program type: kprobe (NOT tracepoint — no `acpi/*` subsystem
 * exists in mainline kernel as of 6.17).
 * Attachment points (kprobe on internal ACPICA functions):
 *   - kprobe/acpi_ev_gpe_dispatch       — GPE dispatched (drivers/acpi/acpica/evgpe.c)
 *   - kprobe/acpi_ns_evaluate           — AML method evaluated (drivers/acpi/acpica/nseval.c)
 *   - kprobe/acpi_irq                   — ACPI SCI handler entry (drivers/acpi/osl.c)
 * NOTE: kprobes on ACPICA internals are fragile across kernel versions;
 * function signatures change without ABI guarantees.  Maintenance burden
 * argues against shipping this as a default sense.
 *
 * ─── MAPS ─────────────────────────────────────────────────────────────
 * - per_gpe: HASH[gpe_num → {fire_count, total_handler_ns,
 *                             max_handler_ns}]
 * - per_method: HASH[method_path[64] → {call_count, total_ns}]
 *
 * ─── COST MODEL ───────────────────────────────────────────────────────
 * Per-event: ~50 ns.  Event rate: typically 0-100/sec on healthy
 * hardware; storms can hit 10K-100K/sec (always pathological).
 * Effectively free always-on.
 *
 * ─── KNOWN LIMITS ─────────────────────────────────────────────────────
 * - acpi_evaluate_method tracepoint requires CONFIG_ACPI=y +
 *   CONFIG_ACPI_DEBUG (most distros; embedded kernels often disable).
 * - VM environments: ACPI is usually quiescent; this facade adds value
 *   on bare metal (and IPMI-managed hardware where firmware is busy).
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 * Sibling: HardIrq (planned, Tier-B) — generic IRQ handler attribution.
 *   ACPI handlers run from IRQ context; HardIrq counts the IRQ, this
 *   classifies the firmware event behind it.
 */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
