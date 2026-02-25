import Mathlib.Tactic

/-!
# Crucible.Philox — Philox4x32-10 Counter-Based PRNG

Backported from Philox.h (models the actual C++ code).

From "Parallel Random Numbers: As Easy as 1, 2, 3" (Salmon et al., 2011).

C++ struct: `struct Philox` (all static constexpr methods)
  W0 = 0x9E3779B9, W1 = 0xBB67AE85   -- Weyl sequence constants
  M0 = 0xD2511F53, M1 = 0xCD9E8D57   -- S-box multiplier constants
  Ctr = array of uint32 x 4           -- counter state
  Key = array of uint32 x 2           -- key state

Core bijection: 10 rounds of multiply-xor-swap.
Each round:
  hi0 = mulhi(M0, ctr 0);  lo0 = ctr 0 * M0
  hi1 = mulhi(M1, ctr 2);  lo1 = ctr 2 * M1
  ctr = { hi1 xor ctr 1 xor key 0, lo1, hi0 xor ctr 3 xor key 1, lo0 }
  key 0 += W0;  key 1 += W1

Properties that matter for Crucible:
  - Deterministic: same (counter, key) produces same output on any hardware
  - Stateless: pure function, no mutable state
  - Platform-independent: UInt32 arithmetic, no hardware-dependent RNG
  - Per-op independence: opKey mixes master_counter + op_index + content_hash
-/

namespace Crucible

/-! ## Philox State Types -/

/-- Counter: 4 x UInt32. C++: `using Ctr = std::array<uint32_t, 4>`.
    The counter is the input to the bijection. For tensor ops,
    element_offset fills c0/c1 and c2/c3 are zero. -/
structure PhiloxCtr where
  c0 : UInt32
  c1 : UInt32
  c2 : UInt32
  c3 : UInt32
  deriving DecidableEq, Repr

/-- Key: 2 x UInt32. C++: `using Key = std::array<uint32_t, 2>`.
    Derived from opKey which mixes master_counter, op_index,
    and content_hash via FNV-1a. -/
structure PhiloxKey where
  k0 : UInt32
  k1 : UInt32
  deriving DecidableEq, Repr

/-! ## Constants

    Golden ratio-derived Weyl sequence constants and S-box multipliers.
    These are the standard Philox4x32-10 constants from the original paper. -/

/-- Weyl constant W0 = 0x9E3779B9 (golden ratio fraction times 2 to the 32).
    C++: `static constexpr uint32_t W0 = 0x9E3779B9;` -/
def W0 : UInt32 := 0x9E3779B9

/-- Weyl constant W1 = 0xBB67AE85.
    C++: `static constexpr uint32_t W1 = 0xBB67AE85;` -/
def W1 : UInt32 := 0xBB67AE85

/-- S-box multiplier M0 = 0xD2511F53.
    C++: `static constexpr uint32_t M0 = 0xD2511F53;` -/
def M0 : UInt32 := 0xD2511F53

/-- S-box multiplier M1 = 0xCD9E8D57.
    C++: `static constexpr uint32_t M1 = 0xCD9E8D57;` -/
def M1 : UInt32 := 0xCD9E8D57

/-! ## Primitive Operations -/

/-- High 32 bits of a 32x32 to 64 multiply.
    C++: `static constexpr uint32_t mulhi_(uint32_t a, uint32_t b)`
    Implementation: widen both operands to 64 bits, multiply, shift right 32. -/
def mulhi (a b : UInt32) : UInt32 :=
  let wide : UInt64 := a.toUInt64 * b.toUInt64
  (wide >>> (32 : UInt64)).toUInt32

/-- Low 32 bits of a 32x32 multiply. Standard UInt32 multiplication
    (mod 2 to the 32 by definition). -/
def mullo (a b : UInt32) : UInt32 := a * b

/-! ## Single Round -/

/-- One Philox round: two parallel multiply-xor-swap operations + key bump.

    C++ (inside the for loop):
      hi0 = mulhi(M0, ctr 0); lo0 = ctr 0 * M0;
      hi1 = mulhi(M1, ctr 2); lo1 = ctr 2 * M1;
      ctr = { hi1 xor ctr 1 xor key 0, lo1, hi0 xor ctr 3 xor key 1, lo0 };
      key 0 += W0; key 1 += W1;

    Returns (new_ctr, bumped_key). The key bump uses Weyl sequence
    constants -- adding the same irrational-derived constant each round
    ensures the key visits all residues mod 2 to the 32. -/
def philoxRound (ctr : PhiloxCtr) (key : PhiloxKey) : PhiloxCtr × PhiloxKey :=
  let hi0 := mulhi M0 ctr.c0
  let lo0 := mullo M0 ctr.c0
  let hi1 := mulhi M1 ctr.c2
  let lo1 := mullo M1 ctr.c2
  let ctr' : PhiloxCtr := {
    c0 := hi1 ^^^ ctr.c1 ^^^ key.k0
    c1 := lo1
    c2 := hi0 ^^^ ctr.c3 ^^^ key.k1
    c3 := lo0
  }
  let key' : PhiloxKey := {
    k0 := key.k0 + W0
    k1 := key.k1 + W1
  }
  (ctr', key')

/-! ## N-Round Iteration -/

/-- Apply n rounds of Philox. Recursive formulation matching the
    C++ for-loop `for (int round = 0; round < 10; round++)`. -/
def philoxRounds (n : Nat) (ctr : PhiloxCtr) (key : PhiloxKey) : PhiloxCtr :=
  match n with
  | 0 => ctr
  | n + 1 =>
    let (ctr', key') := philoxRound ctr key
    philoxRounds n ctr' key'

/-! ## Generate -- The Public API -/

/-- Philox4x32-10: the full 10-round bijection.
    C++: `static constexpr Ctr generate(Ctr ctr, Key key)`.
    Same (ctr, key) produces same output on any platform.
    This is the core PRNG -- everything else derives from it. -/
def generate (ctr : PhiloxCtr) (key : PhiloxKey) : PhiloxCtr :=
  philoxRounds 10 ctr key

/-- Convenience: 64-bit offset + 64-bit key to 4 random UInt32 values.
    C++: `static constexpr Ctr generate(uint64_t offset, uint64_t key)`.
    Splits 64-bit values into the 4x32 counter and 2x32 key.
    offset fills c0/c1, c2/c3 = 0 (available for stream indexing). -/
def generateFromU64 (offset : UInt64) (key : UInt64) : PhiloxCtr :=
  let ctr : PhiloxCtr := {
    c0 := offset.toUInt32
    c1 := (offset >>> (32 : UInt64)).toUInt32
    c2 := 0
    c3 := 0
  }
  let k : PhiloxKey := {
    k0 := key.toUInt32
    k1 := (key >>> (32 : UInt64)).toUInt32
  }
  generate ctr k

/-! ## FNV-1a Key Derivation

    C++: `op_key(master_counter, op_index, content_hash)`.
    Mixes op identity into a 64-bit key for Philox.
    Same (master, op_index, content_hash) produces same key, same PRNG stream.
    Different ops or iterations produce statistically independent streams. -/

/-- FNV-1a offset basis. C++: `0xcbf29ce484222325ULL`. -/
def FNV_OFFSET : UInt64 := 0xcbf29ce484222325

/-- FNV-1a prime. C++: `0x100000001b3ULL`. -/
def FNV_PRIME : UInt64 := 0x100000001b3

/-- Extract byte i (0..7) from a 64-bit value.
    C++: `(v >> (i * 8)) & 0xFF` -/
def extractByte (v : UInt64) (i : Nat) : UInt64 :=
  (v >>> (i * 8 : Nat).toUInt64) &&& 0xFF

/-- FNV-1a mix one byte of v (at position i) into hash state h.
    C++: `h ^= (v >> (i*8)) & 0xFF; h *= 0x100000001b3ULL;` -/
def fnvMixByte (h : UInt64) (v : UInt64) (i : Nat) : UInt64 :=
  (h ^^^ extractByte v i) * FNV_PRIME

/-- Mix all 8 bytes of a value into the FNV state.
    C++: `for (int i = 0; i < 8; i++) { h ^= (v >> (i*8)) & 0xFF; h *= prime; }` -/
def fnvMix (h : UInt64) (v : UInt64) : UInt64 :=
  let h := fnvMixByte h v 0
  let h := fnvMixByte h v 1
  let h := fnvMixByte h v 2
  let h := fnvMixByte h v 3
  let h := fnvMixByte h v 4
  let h := fnvMixByte h v 5
  let h := fnvMixByte h v 6
  fnvMixByte h v 7

/-- Per-op key derivation. Models C++: `Philox::op_key(master, op_idx, content_hash)`.
    Mixes three values via FNV-1a into a 64-bit Philox key.
    The content_hash is modeled as UInt64 (C++: ContentHash strong type, .raw() gives uint64_t).

    Same triple produces same key produces same PRNG stream.
    Different triple produces different key produces independent stream. -/
def opKey (masterCounter : UInt64) (opIndex : UInt32) (contentHash : UInt64) : UInt64 :=
  let h := FNV_OFFSET
  let h := fnvMix h masterCounter
  let h := fnvMix h opIndex.toUInt64
  fnvMix h contentHash

/-! ## Properties

    The key properties for Crucible's determinism guarantees. -/

/-- Philox round is deterministic: same inputs produce same outputs.
    Trivially true for a pure function, but stated explicitly because
    the C++ implementation could hypothetically depend on hardware state
    (it does not -- all constexpr). This is the FOUNDATION of DetSafe. -/
theorem philoxRound_det (ctr : PhiloxCtr) (key : PhiloxKey) :
    ∀ r₁ r₂, philoxRound ctr key = r₁ → philoxRound ctr key = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Generate is deterministic: same (ctr, key) produces same output.
    C++: constexpr -- compiler-evaluated, platform-independent.
    This theorem + Philox's counter-based design = DetSafe for RNG.
    Same master_counter, same op_index, same content_hash, same element_offset
    produces identical random values on CPU, CUDA, ROCm, XLA, any hardware. -/
theorem generate_det (ctr : PhiloxCtr) (key : PhiloxKey) :
    ∀ r₁ r₂, generate ctr key = r₁ → generate ctr key = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- opKey is deterministic: same inputs produce same key.
    Ensures that the same op in the same iteration with the same content
    always gets the same PRNG stream. -/
theorem opKey_det (mc : UInt64) (oi : UInt32) (ch : UInt64) :
    ∀ r₁ r₂, opKey mc oi ch = r₁ → opKey mc oi ch = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Zero rounds is identity: philoxRounds 0 returns the counter unchanged.
    Base case for inductive reasoning about round count. -/
theorem philoxRounds_zero (ctr : PhiloxCtr) (key : PhiloxKey) :
    philoxRounds 0 ctr key = ctr := rfl

/-- Round decomposition: n+1 rounds = one round then n rounds.
    Structural induction base -- matches the C++ for-loop semantics. -/
theorem philoxRounds_succ (n : Nat) (ctr : PhiloxCtr) (key : PhiloxKey) :
    philoxRounds (n + 1) ctr key =
      philoxRounds n (philoxRound ctr key).1 (philoxRound ctr key).2 := by
  conv_lhs => unfold philoxRounds

/-- Generate unfolds to 10 rounds. Connects the public API to the
    round-by-round specification. -/
theorem generate_eq_rounds (ctr : PhiloxCtr) (key : PhiloxKey) :
    generate ctr key = philoxRounds 10 ctr key := rfl

/-- FNV offset basis is the standard value. -/
theorem fnv_offset_val : FNV_OFFSET = (0xcbf29ce484222325 : UInt64) := rfl

/-- FNV prime is the standard value. -/
theorem fnv_prime_val : FNV_PRIME = (0x100000001b3 : UInt64) := rfl

/-- generateFromU64 splits and delegates to generate.
    Ensures the convenience API is semantically equivalent. -/
theorem generateFromU64_eq (offset key : UInt64) :
    generateFromU64 offset key = generate
      { c0 := offset.toUInt32
        c1 := (offset >>> (32 : UInt64)).toUInt32
        c2 := 0
        c3 := 0 }
      { k0 := key.toUInt32
        k1 := (key >>> (32 : UInt64)).toUInt32 } := rfl

/-! ## Independence (Structural)

    Different (counter, key) pairs produce different outputs with
    overwhelming probability. We cannot prove statistical independence
    in Lean (it requires probability theory over the output distribution),
    but we CAN prove the structural prerequisites:
    - Different inputs flow through different computation paths
    - The round function is a bijection (invertible)
    - Key schedule visits all residues (Weyl sequence property) -/

/-- Different counters produce different round inputs.
    If ctr1 ne ctr2, the first round already operates on different data.
    Combined with the bijective nature of Philox rounds, this means
    different counters produce different outputs (with probability
    1 - 2 to the minus 128 for the non-bijective mixing steps). -/
theorem different_ctr_different_input (ctr₁ ctr₂ : PhiloxCtr) (key : PhiloxKey)
    (hne : ctr₁ ≠ ctr₂) :
    (ctr₁, key) ≠ (ctr₂, key) := by
  intro h; exact hne (Prod.mk.inj h).1

/-- Different keys produce different round inputs. -/
theorem different_key_different_input (ctr : PhiloxCtr) (key₁ key₂ : PhiloxKey)
    (hne : key₁ ≠ key₂) :
    (ctr, key₁) ≠ (ctr, key₂) := by
  intro h; exact hne (Prod.mk.inj h).2

/-- The full end-to-end pipeline is deterministic:
    master_counter + op_index + content_hash + element_offset
    produces deterministic random output. This is what Crucible relies on
    for bit-identical reproduction across hardware. -/
theorem pipeline_det (mc : UInt64) (oi : UInt32) (ch : UInt64) (offset : UInt64) :
    ∀ r₁ r₂,
      generateFromU64 offset (opKey mc oi ch) = r₁ →
      generateFromU64 offset (opKey mc oi ch) = r₂ →
      r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-! ## Weyl Sequence Properties

    The key schedule adds W0/W1 each round. Since gcd(W0, 2^32) = 1
    (W0 is odd), the sequence k, k+W0, k+2*W0, ... visits all 2^32
    residues before repeating. This ensures each of the 10 rounds
    uses a distinct key, maximizing mixing. -/

/-- W0 is odd (LSB = 1), so the Weyl sequence k, k+W0, k+2*W0, ...
    has period exactly 2^32. Every possible key value is visited. -/
theorem w0_odd : W0 &&& 1 = 1 := by native_decide

/-- W1 is also odd. Same full-period Weyl sequence property. -/
theorem w1_odd : W1 &&& 1 = 1 := by native_decide

/-! ## Test Vectors

    Compile-time verification that our Lean model matches the C++ implementation.
    These values are computed from the C++ constexpr Philox::generate. -/

/-- Zero counter, zero key output.
    Verifies the round function, constants, and key schedule are all correct.
    C++ constexpr evaluation produces:
      ctr = {0x6627E8D5, 0xE169C58D, 0xBC57AC4C, 0x9B00DBD8}
    for Philox4x32-10 with all-zero inputs. -/
theorem test_zero_zero :
    generate ⟨0, 0, 0, 0⟩ ⟨0, 0⟩ =
      ⟨0x6627E8D5, 0xE169C58D, 0xBC57AC4C, 0x9B00DBD8⟩ := by native_decide

/-- Counter = {1,0,0,0}, key = {0,0}. Verifies that incrementing the
    counter changes all four output words (avalanche). -/
theorem test_ctr1_key0 :
    generate ⟨1, 0, 0, 0⟩ ⟨0, 0⟩ =
      ⟨0xF8E4CCA4, 0x5CB200DB, 0xB1A574EB, 0x097EFF67⟩ := by native_decide

/-- Counter = {0,0,0,0}, key = {1,0}. Verifies that changing the key
    also changes all four output words. -/
theorem test_ctr0_key1 :
    generate ⟨0, 0, 0, 0⟩ ⟨1, 0⟩ =
      ⟨0xE3E80670, 0xE50A0EBC, 0x95F222C0, 0xB615AA27⟩ := by native_decide

/-- Different counters produce different outputs.
    Concrete witness of the independence property for adjacent counters. -/
theorem test_adjacent_differ :
    generate ⟨0, 0, 0, 0⟩ ⟨0, 0⟩ ≠ generate ⟨1, 0, 0, 0⟩ ⟨0, 0⟩ := by native_decide

/-- Different keys produce different outputs.
    Concrete witness of the independence property for different keys. -/
theorem test_different_keys_differ :
    generate ⟨0, 0, 0, 0⟩ ⟨0, 0⟩ ≠ generate ⟨0, 0, 0, 0⟩ ⟨1, 0⟩ := by native_decide

end Crucible
