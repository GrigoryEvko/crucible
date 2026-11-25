/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef __CRUCIBLE_PERF_BPF_RING_TICKET_H
#define __CRUCIBLE_PERF_BPF_RING_TICKET_H

/*
 * Ring ticket
 *   The programs of many CPUs write one ring. Each write claims the next
 *   number of write_idx, its ticket, and the slot of the write is the ticket
 *   masked to the capacity. The commit word of a slot states who is in it. It
 *   is 2t + 1 while the program of the ticket t is in the slot, 2t + 2 when
 *   the record of t is whole, and 0 in a slot that no write reached. The shared
 *   view of fixy/os/OwnedMmap.h reads a record with read_ticket, which takes a
 *   copy only when the commit word is 2t + 2 before the copy and after it.
 *
 *   ring_take_slot takes a slot with one compare-exchange from the commit word
 *   of the ticket one lap before. When that fails, the commit word that it saw
 *   decides. A slot that another program is in, and a slot that holds a later
 *   record, drop the write and add one to dropped. Each other slot is taken
 *   from the commit word that the program saw: in the first lap, and after a
 *   program that dropped. So at most one program is in a slot, no earlier
 *   record replaces a later one, and no program waits.
 *
 *   A BPF exchange and a BPF compare-exchange that succeeds are fully ordered
 *   on each architecture. So a reader that sees a field of a write sees the
 *   odd commit word or a later one when it loads the commit word again, and a
 *   reader that sees 2t + 2 sees each field of the write.
 *
 *   A program that never leaves its slot keeps the slot busy for ever, for
 *   example on a CPU that goes away inside the program. Each later write to
 *   that slot is then dropped and counted, and the ring gives no record for
 *   its ticket. No program takes such a slot over, because the old program can
 *   go on and mix its fields into the new record.
 *
 *   The compare-exchange of 64 bits needs the BPF atomics of Linux 5.12, and
 *   of Linux 5.18 on aarch64. An older kernel refuses the program, and the
 *   facade of the ring reports that it is unavailable.
 */

/* Takes the slot of ticket for one write. False when the write drops. */
static __always_inline bool ring_take_slot(__u64* commit, __u64 ticket, __u64 capacity, __u64* dropped) {
    __u64 busy = 2 * ticket + 1;
    /* In the first lap no slot holds this word, because it is below zero. */
    __u64 before = busy + 1 - 2 * capacity;
    __u64 seen = __sync_val_compare_and_swap(commit, before, busy);
    if (seen == before) return true;
    if ((seen & 1) == 0 && seen < busy && __sync_val_compare_and_swap(commit, seen, busy) == seen) return true;
    __sync_fetch_and_add(dropped, 1);
    return false;
}

/* Ends the write of ticket: its record is whole. */
static __always_inline void ring_give_slot(__u64* commit, __u64 ticket) {
    (void)__sync_lock_test_and_set(commit, 2 * ticket + 2);
}

#endif /* __CRUCIBLE_PERF_BPF_RING_TICKET_H */
