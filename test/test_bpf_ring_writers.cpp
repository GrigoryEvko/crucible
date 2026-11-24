// The instructions of the embedded perf programs hold two rules of the
// sources in include/crucible/perf/bpf/.  The test reads the program text of
// each object and loads no program, so it needs no capability.
//
// Ring writers
//   A program of a perf ring writes a slot as the shared view of
//   fixy/os/OwnedMmap.h asks.  It exchanges ts_ns, the commit word, to zero,
//   it stores each other field, and it exchanges ts_ns to the stamp.  A
//   reader drops a copy that a write overlaps only when the program does
//   both exchanges.  The walk counts the writes with that form: an exchange
//   with a zero, stores of exactly the bytes of the fields, no plain store of
//   the commit word, and a second exchange at the same offset.
//
// Deleted elements
//   A program that ends an interval looks up the element of its start, reads
//   it and deletes it.  The delete gives the element to the next insert of
//   each CPU, so a read after the delete can give the data of another thread.
//   The walk follows the registers that hold the address of the first element
//   that the program looks up.  It refuses a load through one of them after
//   the first delete.  It does not follow an address through the stack.
//
// The two walks read the instructions in the order of the text.  clang writes
// the code between the two exchanges, and between the lookup and the delete,
// with no jump back.

#include <crucible/perf/detail/BpfLoader.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "bpf_object_reader.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace perf = crucible::perf::detail;

namespace {

using ::crucible::test::LittleEndian;
using ::crucible::test::ObjectBytes;
using ::crucible::test::ObjectSection;

// An instruction has eight bytes: the opcode, the two registers, an offset of
// two bytes and an immediate of four bytes (include/uapi/linux/bpf.h).
inline constexpr std::size_t kInstructionBytes = 8;

// The helpers that the walks name, by their number.
enum class Helper : std::uint8_t {
    map_lookup_elem = 1,
    map_delete_elem = 3,
};

// A register, by its number of four bits.  A call gives its result in r0, and
// the source of a call of a helper is 0 too.  r10 is the frame pointer.
using Register = ::fixy::Index<16>;
inline constexpr Register kResult = ::fixy::index<0>();
inline constexpr Register kFramePointer = ::fixy::index<10>();

// One instruction, with the fields that the walks read.  The low three bits
// of an opcode are its class: 0 for a load of an immediate, 1 for a load of
// memory, 2 and 3 for a store, 4 and 7 for arithmetic, 5 and 6 for a jump.
struct Instruction {
    std::uint64_t opcode = 0;
    Register destination = kResult;
    Register source = kResult;
    std::uint64_t offset = 0;
    std::uint64_t immediate = 0;

    // The instruction at the byte offset of text.
    [[nodiscard]] static Instruction at(LittleEndian text, std::size_t byte_offset) noexcept {
        std::uint64_t const registers = text.word<1>(byte_offset + 1);
        return Instruction{.opcode = text.word<1>(byte_offset),
                           .destination = Register::mask(registers),
                           .source = Register::mask(registers >> 4),
                           .offset = text.word<2>(byte_offset + 2),
                           .immediate = text.word<4>(byte_offset + 4)};
    }

    [[nodiscard]] std::uint64_t opcode_class() const noexcept { return opcode & 0x07u; }

    // BPF_STX | BPF_ATOMIC | BPF_DW with BPF_XCHG: the exchange of 64 bits.
    [[nodiscard]] bool is_exchange() const noexcept { return opcode == 0xDBu && immediate == 0xE1u; }

    [[nodiscard]] bool is_call() const noexcept { return opcode == 0x85u; }
    [[nodiscard]] bool is_call_of(Helper helper) const noexcept {
        return is_call() && source == kResult && immediate == static_cast<std::uint64_t>(helper);
    }
    [[nodiscard]] bool is_exit() const noexcept { return opcode == 0x95u; }

    // A plain store of a register or of an immediate (BPF_MEM is 0x60).
    [[nodiscard]] bool is_plain_store() const noexcept {
        return (opcode_class() == 2 || opcode_class() == 3) && (opcode & 0xE0u) == 0x60u;
    }

    // A store whose base is not the frame pointer r10, so a store of memory
    // that the program shares.
    [[nodiscard]] bool is_shared_store() const noexcept { return is_plain_store() && destination != kFramePointer; }

    // The bytes of the access of a load or a store.
    [[nodiscard]] std::uint64_t access_bytes() const noexcept {
        switch (opcode & 0x18u) {
            case 0x00u:
                return 4;
            case 0x08u:
                return 2;
            case 0x10u:
                return 1;
            default:
                return 8;
        }
    }

    [[nodiscard]] bool is_load() const noexcept { return opcode_class() == 1; }

    // A move of a 64-bit register, or a move of an immediate.
    [[nodiscard]] bool is_register_move() const noexcept { return opcode == 0xBFu; }
    [[nodiscard]] bool is_zero_move() const noexcept { return (opcode == 0xB7u || opcode == 0xB4u) && immediate == 0; }

    // An add to a 64-bit register, which keeps an address in its element.
    [[nodiscard]] bool is_add() const noexcept { return opcode == 0x07u || opcode == 0x0Fu; }

    // An instruction that gives its destination register a new value.
    [[nodiscard]] bool sets_destination() const noexcept {
        std::uint64_t const group = opcode_class();
        return group == 0 || group == 1 || group == 4 || group == 7;
    }
};

// A set of registers.
class RegisterSet {
public:
    [[nodiscard]] bool has(Register held) const noexcept { return (bits_ & bit_of(held)) != 0; }
    void add(Register held) noexcept { bits_ |= bit_of(held); }
    void drop(Register held) noexcept { bits_ &= ~bit_of(held); }

    // A call gives r0 a new value and can change r1 thru r5.
    void drop_call_registers() noexcept { bits_ &= ~std::uint64_t{0x3F}; }

private:
    [[nodiscard]] static std::uint64_t bit_of(Register held) noexcept { return std::uint64_t{1} << held.raw(); }

    std::uint64_t bits_ = 0;
};

// The writes that one object must hold.
struct RingWriter {
    ::fixy::TextView name{};
    // The count of the programs of the object that write its ring.
    std::size_t writers = 0;
    // The bytes of the fields of a record, other than ts_ns and the padding.
    std::uint64_t field_bytes = 0;
};

// The count of the writes of text that have the form of the top of the file,
// or no value when a write has another form.  Complexity: linear in the
// instructions of text.
[[nodiscard]] ::fixy::Option<std::size_t> count_committed_writes(ObjectBytes text, RingWriter const& ring) noexcept {
    LittleEndian const words{text};
    RegisterSet zeros{};
    std::size_t writes = 0;
    bool is_inside = false;
    std::uint64_t commit_offset = 0;
    std::uint64_t stored = 0;
    for (std::size_t place = 0; place < text.size(); place += kInstructionBytes) {
        Instruction const instruction = Instruction::at(words, place);
        if (instruction.is_exchange()) {
            bool const is_start = !is_inside && zeros.has(instruction.source);
            bool const is_end = is_inside && !zeros.has(instruction.source) && instruction.offset == commit_offset
                             && stored == ring.field_bytes;
            if (!is_start && !is_end) {
                return ::fixy::none;
            }
            commit_offset = instruction.offset;
            stored = 0;
            is_inside = is_start;
            zeros.drop(instruction.source);
            if (is_end) ++writes;
            continue;
        }
        if (is_inside
            && (instruction.is_exit() || (instruction.is_shared_store() && instruction.offset == commit_offset))) {
            return ::fixy::none;
        }
        if (is_inside && instruction.is_shared_store()) stored += instruction.access_bytes();
        if (instruction.is_call()) {
            zeros.drop_call_registers();
        } else if (instruction.is_zero_move()) {
            zeros.add(instruction.destination);
        } else if (instruction.sets_destination()) {
            zeros.drop(instruction.destination);
        }
    }
    if (is_inside) {
        return ::fixy::none;
    }
    return ::fixy::some(writes);
}

// What a text does with the element of its first lookup.
enum class ElementReads : std::uint8_t {
    before_delete_only,
    after_delete,
    // The text looks up no element, reads none, or deletes none.
    no_interval,
};

// The reads of the element that text looks up first (the deleted elements).
// Complexity: linear in the instructions of text.
[[nodiscard]] ElementReads reads_of_first_element(ObjectBytes text) noexcept {
    LittleEndian const words{text};
    RegisterSet element{};
    bool has_lookup = false;
    bool has_read = false;
    bool has_delete = false;
    for (std::size_t place = 0; place < text.size(); place += kInstructionBytes) {
        Instruction const instruction = Instruction::at(words, place);
        if (instruction.is_call()) {
            element.drop_call_registers();
            if (!has_lookup && instruction.is_call_of(Helper::map_lookup_elem)) {
                has_lookup = true;
                element.add(kResult);
            }
            if (has_lookup && instruction.is_call_of(Helper::map_delete_elem)) has_delete = true;
            continue;
        }
        if (instruction.is_load() && element.has(instruction.source)) {
            if (has_delete) {
                return ElementReads::after_delete;
            }
            has_read = true;
        }
        if (instruction.is_register_move() && element.has(instruction.source)) {
            element.add(instruction.destination);
        } else if (instruction.sets_destination() && !instruction.is_add()) {
            element.drop(instruction.destination);
        }
    }
    return has_read && has_delete ? ElementReads::before_delete_only : ElementReads::no_interval;
}

// The bytes of the one section of object that holds instructions and has the
// name.
[[nodiscard]] ObjectBytes program_text(perf::ForeignBytes object, ::fixy::TextView section) noexcept {
    return ::crucible::test::section_named(LittleEndian{perf::view_bytes(object)}, section)
        .expect("the object holds the program section");
}

// Compares the writes of each program section of object with ring.
void expect_ring_writes(perf::ForeignBytes object, RingWriter const& ring) noexcept {
    LittleEndian const words{perf::view_bytes(object)};
    std::size_t writes = 0;
    for (std::size_t index = 0; index < ObjectSection::section_count(words); ++index) {
        ObjectSection const section{words, index};
        if (!section.has_instructions()) continue;
        writes += count_committed_writes(section.bytes(), ring).expect("each write of the ring has the committed form");
    }
    if (writes != ring.writers) {
        ::fixy::report(::fixy::Sink::Err, "  {}: {} committed writes, and {} programs write the ring\n", ring.name,
                       writes, ring.writers);
    }
    CRUCIBLE_TEST_ASSERT(kTest, writes == ring.writers);
    ::fixy::check::pass(kTest, "  {}: {} committed writes\n", ring.name, writes);
}

void test_each_ring_program_commits_its_slots() {
    // off_cpu_ns, tid and on_cpu.
    expect_ring_writes(perf::sched_switch_bpf_object(),
                       RingWriter{.name = "sched_switch", .writers = 1, .field_bytes = 16});
    expect_ring_writes(perf::sched_tp_btf_bpf_object(),
                       RingWriter{.name = "sched_tp_btf", .writers = 1, .field_bytes = 16});
    // duration_ns, tid and syscall_nr.
    expect_ring_writes(perf::syscall_latency_bpf_object(),
                       RingWriter{.name = "syscall_latency", .writers = 1, .field_bytes = 16});
    expect_ring_writes(perf::syscall_tp_btf_bpf_object(),
                       RingWriter{.name = "syscall_tp_btf", .writers = 1, .field_bytes = 16});
    // futex_addr, wait_ns and tid.
    expect_ring_writes(perf::lock_contention_bpf_object(),
                       RingWriter{.name = "lock_contention", .writers = 1, .field_bytes = 20});
    // ip, tid and event_type, in each of the eight programs.
    expect_ring_writes(perf::pmu_sample_bpf_object(),
                       RingWriter{.name = "pmu_sample", .writers = 8, .field_bytes = 13});
    ::fixy::check::pass(kTest, "  test_each_ring_program_commits_its_slots: PASSED\n");
}

void test_no_program_reads_a_deleted_element() {
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_first_element(program_text(perf::lock_contention_bpf_object(),
                                                                    "tracepoint/syscalls/sys_exit_futex"))
                                    == ElementReads::before_delete_only);
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_first_element(program_text(perf::syscall_latency_bpf_object(),
                                                                    "tracepoint/raw_syscalls/sys_exit"))
                                    == ElementReads::before_delete_only);
    CRUCIBLE_TEST_ASSERT(kTest,
                         reads_of_first_element(program_text(perf::syscall_tp_btf_bpf_object(), "tp_btf/sys_exit"))
                             == ElementReads::before_delete_only);
    ::fixy::check::pass(kTest, "  test_no_program_reads_a_deleted_element: PASSED\n");
}

// Each text is the encoding of some instructions, one for each row.  The
// register byte holds the source in its high half and the destination in its
// low half.
void test_the_walks_refuse_a_wrong_text() {
    RingWriter const ring{.name = "text", .writers = 1, .field_bytes = 8};
    ::fixy::FixedArray<unsigned char, 32> const committed{
        0xB7, 0x01, 0, 0, 0,    0, 0, 0,  // r1 = 0
        0xDB, 0x10, 8, 0, 0xE1, 0, 0, 0,  // r1 = xchg_64(r0 + 8, r1)
        0x7B, 0x60, 0, 0, 0,    0, 0, 0,  // *(u64 *)(r0 + 0) = r6
        0xDB, 0x70, 8, 0, 0xE1, 0, 0, 0,  // r7 = xchg_64(r0 + 8, r7)
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_committed_writes(committed.view(), ring).value_or(0) == 1);

    // The last store is plain, so the write does not end.
    ::fixy::FixedArray<unsigned char, 32> const plain_stamp{
        0xB7, 0x01, 0, 0, 0,    0, 0, 0,  // r1 = 0
        0xDB, 0x10, 8, 0, 0xE1, 0, 0, 0,  // r1 = xchg_64(r0 + 8, r1)
        0x7B, 0x60, 0, 0, 0,    0, 0, 0,  // *(u64 *)(r0 + 0) = r6
        0x7B, 0x70, 8, 0, 0,    0, 0, 0,  // *(u64 *)(r0 + 8) = r7
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_committed_writes(plain_stamp.view(), ring).is_none());

    // The first exchange stores the stamp and not a zero.
    ::fixy::FixedArray<unsigned char, 32> const stamp_first{
        0xB7, 0x01, 0, 0, 1,    0, 0, 0,  // r1 = 1
        0xDB, 0x10, 8, 0, 0xE1, 0, 0, 0,  // r1 = xchg_64(r0 + 8, r1)
        0x7B, 0x60, 0, 0, 0,    0, 0, 0,  // *(u64 *)(r0 + 0) = r6
        0xDB, 0x70, 8, 0, 0xE1, 0, 0, 0,  // r7 = xchg_64(r0 + 8, r7)
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_committed_writes(stamp_first.view(), ring).is_none());

    // The field store is before the first exchange.
    ::fixy::FixedArray<unsigned char, 32> const field_first{
        0x7B, 0x60, 0, 0, 0,    0, 0, 0,  // *(u64 *)(r0 + 0) = r6
        0xB7, 0x01, 0, 0, 0,    0, 0, 0,  // r1 = 0
        0xDB, 0x10, 8, 0, 0xE1, 0, 0, 0,  // r1 = xchg_64(r0 + 8, r1)
        0xDB, 0x70, 8, 0, 0xE1, 0, 0, 0,  // r7 = xchg_64(r0 + 8, r7)
    };
    CRUCIBLE_TEST_ASSERT(kTest, count_committed_writes(field_first.view(), ring).is_none());

    // The load after the delete reads the element that r7 names.
    ::fixy::FixedArray<unsigned char, 40> const read_after_delete{
        0x85, 0x00, 0, 0, 1, 0, 0, 0,  // call map_lookup_elem
        0xBF, 0x07, 0, 0, 0, 0, 0, 0,  // r7 = r0
        0x79, 0x08, 8, 0, 0, 0, 0, 0,  // r8 = *(u64 *)(r0 + 8)
        0x85, 0x00, 0, 0, 3, 0, 0, 0,  // call map_delete_elem
        0x79, 0x71, 0, 0, 0, 0, 0, 0,  // r1 = *(u64 *)(r7 + 0)
    };
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_first_element(read_after_delete.view()) == ElementReads::after_delete);

    // The load after the delete reads the stack.
    ::fixy::FixedArray<unsigned char, 40> const read_of_the_stack{
        0x85, 0x00, 0,    0,    1, 0, 0, 0,  // call map_lookup_elem
        0xBF, 0x07, 0,    0,    0, 0, 0, 0,  // r7 = r0
        0x79, 0x08, 8,    0,    0, 0, 0, 0,  // r8 = *(u64 *)(r0 + 8)
        0x85, 0x00, 0,    0,    3, 0, 0, 0,  // call map_delete_elem
        0x79, 0xA1, 0xE8, 0xFF, 0, 0, 0, 0,  // r1 = *(u64 *)(r10 - 24)
    };
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_first_element(read_of_the_stack.view()) == ElementReads::before_delete_only);
    ::fixy::check::pass(kTest, "  test_the_walks_refuse_a_wrong_text: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_bpf_ring_writers:\n");
    test_the_walks_refuse_a_wrong_text();
    test_each_ring_program_commits_its_slots();
    test_no_program_reads_a_deleted_element();
    ::fixy::check::pass(kTest, "test_bpf_ring_writers: all PASSED\n");
    return 0;
}
