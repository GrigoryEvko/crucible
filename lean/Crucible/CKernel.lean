import Mathlib.Tactic

/-!
# Crucible.CKernel -- CKernel Taxonomy Formalization

Models CKernel.h: 146 device-agnostic abstract compute ops plus OPAQUE (fallback).
Two sections: Section 1 (ordinals 1-99, frozen since CDAG_VERSION=3) covers core DNN,
Section 2 (ordinals 100-146, added in v4) covers extended ops.

C++ type: `enum class CKernelId : uint8_t` with `NUM_KERNELS = 147` sentinel.
Registration model: Vessel registers schema_hash -> CKernelId at startup,
`classify_kernel()` returns OPAQUE until registered. Sorted array + binary search.

Key invariants proved here:
- All 147 ordinals (0..146) are distinct
- Section 1 occupies exactly ordinals 1..99 (99 ops)
- Section 2 occupies exactly ordinals 100..146 (47 ops)
- Total non-OPAQUE ops = 146
- Every ordinal fits in uint8_t (< 256)
- Family membership is decidable and exhaustive
-/

namespace Crucible

/-! ## Compute-op families

Each CKernelId belongs to exactly one family. Families group ops by compute pattern:
linear algebra ops share cuBLAS dispatch, reductions share warp-level primitives, etc.
The family determines which Tier 2 code path handles the op. -/

/-- Compute-op family. Determines the Tier 2 dispatch strategy and kernel template. -/
inductive CKernelFamily where
  | LinAlg          -- GEMM variants (cuBLAS/cuBLASLt)
  | Conv            -- convolution (cuDNN/Winograd)
  | Attention       -- scaled dot-product attention and variants
  | Norm            -- normalization (layer/batch/group/instance/RMS)
  | Activation      -- pointwise nonlinearities
  | EwiseBinary     -- elementwise binary (add, mul, ...)
  | EwiseUnary      -- elementwise unary (exp, log, ...)
  | Reduction       -- sum, mean, argmax, topk, ...
  | Pooling         -- spatial pooling
  | DataMovement    -- view, reshape, permute, index, scatter, ...
  | Embedding       -- lookup tables
  | Copy            -- copy_, clone
  | Vision          -- interpolate, grid_sample, im2col
  | Fused           -- fused multi-op blocks (attention, SwiGLU, ...)
  | LinAlgDecomp    -- SVD, Cholesky, QR, ... (cuSOLVER/MAGMA)
  | SSM             -- state-space models / recurrence (Mamba, RWKV, ...)
  | ProdInference   -- production serving (Marlin, PagedAttention, MoE, ...)
  | Rendering3D     -- neural rendering (3DGS, NeRF, Instant-NGP)
  | StructuredGraph  -- structured matrices and GNN primitives
  | Comm            -- collective communication (NCCL/RCCL/Gloo)
  | IOOp            -- data loading and checkpointing
  | RNG             -- random number generation (Philox dispatch)
  | Opaque          -- unknown op, fallback to full Vessel dispatch
  deriving DecidableEq, Repr

/-! ## CKernelId -- the 147-element enumeration

Modeled as an inductive type with one constructor per op. The `toOrdinal` function
assigns the C++ uint8_t value. Lean's kernel computes exhaustive decidable equality
from the inductive structure. -/

/-- Device-agnostic abstract compute-op identity.
    C++: `enum class CKernelId : uint8_t`. 147 values (0 = OPAQUE, 1..146 = ops).
    Multiple ATen dispatch names may alias to one CKernelId. -/
inductive CKernelId where
  -- Fallback
  | OPAQUE              -- 0: unknown op, full Vessel dispatch

  -- Section 1: Core DNN Ops (ordinals 1-99, frozen since CDAG_VERSION=3)

  -- Linear Algebra (8)
  | GEMM_MM             -- 1: aten::mm
  | GEMM_BMM            -- 2: aten::bmm
  | GEMM_MATMUL         -- 3: aten::matmul
  | GEMM_ADDMM          -- 4: aten::addmm (Linear hot path)
  | GEMM_LINEAR         -- 5: aten::linear
  | GEMM_ADDBMM         -- 6: aten::addbmm
  | GEMM_BADDBMM        -- 7: aten::baddbmm
  | GEMM_EINSUM         -- 8: aten::einsum

  -- Convolution (6)
  | CONV1D              -- 9
  | CONV2D              -- 10
  | CONV3D              -- 11
  | CONV_TRANSPOSE1D    -- 12
  | CONV_TRANSPOSE2D    -- 13
  | CONV_TRANSPOSE3D    -- 14

  -- Attention (4)
  | SDPA                -- 15: scaled_dot_product_attention (+ 5 backend variants)
  | MHA                 -- 16: multi_head_attention_forward
  | ROPE                -- 17: rotary position embedding
  | POSITION_BIAS       -- 18: ALiBi and additive position bias

  -- Normalization (6)
  | LAYER_NORM          -- 19: layer_norm / native_layer_norm
  | BATCH_NORM_TRAIN    -- 20: batch_norm (training=true)
  | BATCH_NORM_EVAL     -- 21: batch_norm inference
  | GROUP_NORM          -- 22: group_norm / native_group_norm
  | INSTANCE_NORM       -- 23
  | RMS_NORM            -- 24: LLaMA/Mistral

  -- Activations (13)
  | ACT_RELU            -- 25
  | ACT_GELU            -- 26: exact and tanh-approximation
  | ACT_SILU            -- 27: swish
  | ACT_SIGMOID         -- 28
  | ACT_TANH            -- 29
  | ACT_HARDSWISH       -- 30
  | ACT_LEAKY_RELU      -- 31
  | ACT_ELU             -- 32: elu / selu / celu
  | ACT_SOFTMAX         -- 33: softmax / _softmax
  | ACT_LOG_SOFTMAX     -- 34: log_softmax / _log_softmax
  | ACT_DROPOUT         -- 35: stochastic mask (training) / identity (inference)
  | ACT_CLAMP           -- 36: clamp / hardtanh / relu6
  | ACT_MISH            -- 37

  -- Elementwise Binary (9)
  | EWISE_ADD           -- 38: inplace variants folded in
  | EWISE_MUL           -- 39
  | EWISE_SUB           -- 40
  | EWISE_DIV           -- 41
  | EWISE_POW           -- 42
  | EWISE_MAX           -- 43: elementwise maximum (not reduction)
  | EWISE_MIN           -- 44: elementwise minimum (not reduction)
  | EWISE_MOD           -- 45: fmod / remainder
  | EWISE_WHERE         -- 46: ternary broadcast select

  -- Elementwise Unary (10)
  | EWISE_EXP           -- 47: exp / exp2
  | EWISE_LOG           -- 48: log / log2 / log10
  | EWISE_SQRT          -- 49
  | EWISE_RSQRT         -- 50: 1/sqrt(var+eps)
  | EWISE_ABS           -- 51
  | EWISE_NEG           -- 52
  | EWISE_SIGN          -- 53
  | EWISE_FLOOR         -- 54: floor / ceil / round / trunc
  | EWISE_CAST          -- 55: dtype conversion
  | EWISE_FILL          -- 56: broadcast scalar into storage

  -- Reductions (8)
  | REDUCE_SUM          -- 57: sum / nansum
  | REDUCE_MEAN         -- 58
  | REDUCE_MAX          -- 59: max / amax
  | REDUCE_MIN          -- 60: min / amin
  | REDUCE_ARGMAX       -- 61
  | REDUCE_ARGMIN       -- 62
  | REDUCE_CUMSUM       -- 63: cumsum / cumprod
  | REDUCE_TOPK         -- 64: values + indices

  -- Pooling (8)
  | POOL_MAX1D          -- 65
  | POOL_MAX2D          -- 66
  | POOL_MAX3D          -- 67
  | POOL_AVG1D          -- 68
  | POOL_AVG2D          -- 69
  | POOL_AVG3D          -- 70
  | POOL_ADAPTIVE_MAX   -- 71
  | POOL_ADAPTIVE_AVG   -- 72

  -- Data Movement / Indexing (16)
  | VIEW                -- 73: non-copying reshape
  | RESHAPE             -- 74: may copy if non-contiguous
  | PERMUTE             -- 75: arbitrary axis reorder
  | TRANSPOSE           -- 76: swap two axes
  | CONTIGUOUS          -- 77: force C-contiguous
  | EXPAND              -- 78: broadcast via stride-0
  | SQUEEZE             -- 79: squeeze / unsqueeze
  | SLICE               -- 80: narrow / select
  | INDEX_SELECT        -- 81: 1-D integer index lookup
  | INDEX               -- 82: advanced indexing
  | SCATTER             -- 83: scatter / scatter_add / scatter_reduce
  | MASKED_FILL         -- 84: attention mask application
  | PAD                 -- 85
  | CAT                 -- 86: concatenate along dim
  | STACK               -- 87: concatenate with new dim
  | UNFOLD              -- 88: sliding window

  -- Embedding (2)
  | EMBEDDING           -- 89: dense lookup table
  | EMBEDDING_BAG       -- 90: lookup + pooling

  -- Copy (2)
  | COPY_               -- 91: cross-device/dtype in-place copy
  | CLONE               -- 92: always allocates

  -- Vision (3)
  | INTERPOLATE         -- 93: bilinear / nearest / bicubic
  | GRID_SAMPLE         -- 94: spatial transformer
  | IM2COL              -- 95: explicit patch extraction

  -- Fused high-level (4)
  | FUSED_ATTENTION     -- 96: full MHSA block (FlashAttention-2/3)
  | FUSED_LINEAR_ACT    -- 97: linear + activation gate (SwiGLU, GeGLU)
  | FUSED_NORM_LINEAR   -- 98: pre-norm + linear (T5/LLaMA)
  | FUSED_SOFTMAX_DROP  -- 99: softmax + dropout

  -- Section 2: Extended Ops (ordinals 100-146, added in CDAG_VERSION=4)

  -- Linear Algebra Decompositions (9)
  | LINALG_SVD          -- 100: cuSOLVER
  | LINALG_CHOLESKY     -- 101
  | LINALG_QR           -- 102
  | LINALG_SOLVE        -- 103
  | LINALG_EIGH         -- 104
  | LINALG_NORM         -- 105
  | LINALG_CROSS        -- 106
  | CDIST               -- 107: pairwise distance
  | FFT                 -- 108: fast Fourier transform

  -- SSM / Recurrence (6)
  | ASSOC_SCAN          -- 109: generic parallel prefix scan
  | SELECTIVE_SCAN      -- 110: Mamba S6
  | SSD_CHUNK           -- 111: Mamba-2 chunked semiseparable
  | WKV_RECURRENCE      -- 112: RWKV
  | RETENTION           -- 113: RetNet
  | MLSTM_RECURRENCE    -- 114: xLSTM

  -- Production Inference (6)
  | DEQUANT_GEMM        -- 115: INT4/FP8 dequant + GEMM (Marlin/AWQ)
  | MOE_ROUTE_GEMM      -- 116: MoE top-k routing + grouped GEMM
  | PAGED_ATTENTION     -- 117: page-table-indirect KV (vLLM)
  | FUSED_CROSS_ENTROPY -- 118: tiled matmul + online softmax (Liger)
  | LINEAR_ATTN_CAUSAL  -- 119: chunked cumulative outer-product (GLA)
  | RAGGED_ATTN         -- 120: variable-length packed sequences (THD)

  -- 3D / Neural Rendering (4)
  | GAUSSIAN_RASTERIZE  -- 121: 3DGS tile-sort-alpha-blend
  | HASH_GRID_ENCODE    -- 122: Instant-NGP multi-resolution hash
  | VOLUME_RENDER       -- 123: NeRF per-ray compositing
  | SH_EVAL             -- 124: spherical harmonics basis

  -- Structured Matrix / Graph (5)
  | FFT_CONV            -- 125: fused FFT + pointwise mul + IFFT (Hyena/H3)
  | MONARCH_MATMUL      -- 126: block-diagonal GEMM + permutation
  | SPMM_GNN            -- 127: graph message passing
  | SDDMM_GNN           -- 128: sampled dense-dense matmul
  | SINKHORN            -- 129: iterative optimal transport

  -- Collective Communication (10)
  | COMM_ALLREDUCE      -- 130: DDP / FSDP / ZeRO
  | COMM_ALLGATHER      -- 131: FSDP forward
  | COMM_REDUCE_SCATTER -- 132: FSDP backward
  | COMM_BROADCAST      -- 133: parameter broadcast
  | COMM_ALL_TO_ALL     -- 134: MoE expert routing
  | COMM_SEND           -- 135: pipeline parallel
  | COMM_RECV           -- 136: pipeline parallel
  | COMM_REDUCE         -- 137: parameter server
  | COMM_GATHER         -- 138: centralized logging
  | COMM_SCATTER        -- 139: data distribution

  -- I/O (4)
  | IO_LOAD             -- 140
  | IO_PREFETCH         -- 141: async CPU -> GPU DMA
  | IO_CHECKPOINT_SAVE  -- 142
  | IO_CHECKPOINT_LOAD  -- 143

  -- RNG (2)
  | RNG_UNIFORM         -- 144: rand / randint / bernoulli
  | RNG_NORMAL          -- 145: randn / normal_

  -- Synchronization (1)
  | COMM_BARRIER        -- 146: all-rank barrier
  deriving DecidableEq, Repr

/-! ## Ordinal assignment

Maps each CKernelId to its C++ uint8_t ordinal. This is the ground truth that
all other properties are proved against. The C++ sentinel `NUM_KERNELS = 147`
is not a valid CKernelId -- it exists only for array sizing. -/

/-- The C++ ordinal (uint8_t value) for each CKernelId.
    C++: implicit from enum declaration order, OPAQUE = 0, NUM_KERNELS = 147. -/
def CKernelId.toOrdinal : CKernelId -> Nat
  | .OPAQUE => 0
  -- Section 1: Linear Algebra
  | .GEMM_MM => 1 | .GEMM_BMM => 2 | .GEMM_MATMUL => 3 | .GEMM_ADDMM => 4
  | .GEMM_LINEAR => 5 | .GEMM_ADDBMM => 6 | .GEMM_BADDBMM => 7 | .GEMM_EINSUM => 8
  -- Convolution
  | .CONV1D => 9 | .CONV2D => 10 | .CONV3D => 11
  | .CONV_TRANSPOSE1D => 12 | .CONV_TRANSPOSE2D => 13 | .CONV_TRANSPOSE3D => 14
  -- Attention
  | .SDPA => 15 | .MHA => 16 | .ROPE => 17 | .POSITION_BIAS => 18
  -- Normalization
  | .LAYER_NORM => 19 | .BATCH_NORM_TRAIN => 20 | .BATCH_NORM_EVAL => 21
  | .GROUP_NORM => 22 | .INSTANCE_NORM => 23 | .RMS_NORM => 24
  -- Activations
  | .ACT_RELU => 25 | .ACT_GELU => 26 | .ACT_SILU => 27 | .ACT_SIGMOID => 28
  | .ACT_TANH => 29 | .ACT_HARDSWISH => 30 | .ACT_LEAKY_RELU => 31 | .ACT_ELU => 32
  | .ACT_SOFTMAX => 33 | .ACT_LOG_SOFTMAX => 34 | .ACT_DROPOUT => 35
  | .ACT_CLAMP => 36 | .ACT_MISH => 37
  -- Elementwise Binary
  | .EWISE_ADD => 38 | .EWISE_MUL => 39 | .EWISE_SUB => 40 | .EWISE_DIV => 41
  | .EWISE_POW => 42 | .EWISE_MAX => 43 | .EWISE_MIN => 44
  | .EWISE_MOD => 45 | .EWISE_WHERE => 46
  -- Elementwise Unary
  | .EWISE_EXP => 47 | .EWISE_LOG => 48 | .EWISE_SQRT => 49 | .EWISE_RSQRT => 50
  | .EWISE_ABS => 51 | .EWISE_NEG => 52 | .EWISE_SIGN => 53
  | .EWISE_FLOOR => 54 | .EWISE_CAST => 55 | .EWISE_FILL => 56
  -- Reductions
  | .REDUCE_SUM => 57 | .REDUCE_MEAN => 58 | .REDUCE_MAX => 59 | .REDUCE_MIN => 60
  | .REDUCE_ARGMAX => 61 | .REDUCE_ARGMIN => 62 | .REDUCE_CUMSUM => 63 | .REDUCE_TOPK => 64
  -- Pooling
  | .POOL_MAX1D => 65 | .POOL_MAX2D => 66 | .POOL_MAX3D => 67
  | .POOL_AVG1D => 68 | .POOL_AVG2D => 69 | .POOL_AVG3D => 70
  | .POOL_ADAPTIVE_MAX => 71 | .POOL_ADAPTIVE_AVG => 72
  -- Data Movement
  | .VIEW => 73 | .RESHAPE => 74 | .PERMUTE => 75 | .TRANSPOSE => 76
  | .CONTIGUOUS => 77 | .EXPAND => 78 | .SQUEEZE => 79 | .SLICE => 80
  | .INDEX_SELECT => 81 | .INDEX => 82 | .SCATTER => 83 | .MASKED_FILL => 84
  | .PAD => 85 | .CAT => 86 | .STACK => 87 | .UNFOLD => 88
  -- Embedding
  | .EMBEDDING => 89 | .EMBEDDING_BAG => 90
  -- Copy
  | .COPY_ => 91 | .CLONE => 92
  -- Vision
  | .INTERPOLATE => 93 | .GRID_SAMPLE => 94 | .IM2COL => 95
  -- Fused
  | .FUSED_ATTENTION => 96 | .FUSED_LINEAR_ACT => 97
  | .FUSED_NORM_LINEAR => 98 | .FUSED_SOFTMAX_DROP => 99
  -- Section 2: Linear Algebra Decompositions
  | .LINALG_SVD => 100 | .LINALG_CHOLESKY => 101 | .LINALG_QR => 102
  | .LINALG_SOLVE => 103 | .LINALG_EIGH => 104 | .LINALG_NORM => 105
  | .LINALG_CROSS => 106 | .CDIST => 107 | .FFT => 108
  -- SSM / Recurrence
  | .ASSOC_SCAN => 109 | .SELECTIVE_SCAN => 110 | .SSD_CHUNK => 111
  | .WKV_RECURRENCE => 112 | .RETENTION => 113 | .MLSTM_RECURRENCE => 114
  -- Production Inference
  | .DEQUANT_GEMM => 115 | .MOE_ROUTE_GEMM => 116 | .PAGED_ATTENTION => 117
  | .FUSED_CROSS_ENTROPY => 118 | .LINEAR_ATTN_CAUSAL => 119 | .RAGGED_ATTN => 120
  -- 3D / Neural Rendering
  | .GAUSSIAN_RASTERIZE => 121 | .HASH_GRID_ENCODE => 122
  | .VOLUME_RENDER => 123 | .SH_EVAL => 124
  -- Structured Matrix / Graph
  | .FFT_CONV => 125 | .MONARCH_MATMUL => 126 | .SPMM_GNN => 127
  | .SDDMM_GNN => 128 | .SINKHORN => 129
  -- Collective Communication
  | .COMM_ALLREDUCE => 130 | .COMM_ALLGATHER => 131 | .COMM_REDUCE_SCATTER => 132
  | .COMM_BROADCAST => 133 | .COMM_ALL_TO_ALL => 134 | .COMM_SEND => 135
  | .COMM_RECV => 136 | .COMM_REDUCE => 137 | .COMM_GATHER => 138
  | .COMM_SCATTER => 139
  -- I/O
  | .IO_LOAD => 140 | .IO_PREFETCH => 141
  | .IO_CHECKPOINT_SAVE => 142 | .IO_CHECKPOINT_LOAD => 143
  -- RNG
  | .RNG_UNIFORM => 144 | .RNG_NORMAL => 145
  -- Synchronization
  | .COMM_BARRIER => 146

/-! ## Family classification

Maps each CKernelId to its compute family. Determines which Tier 2 code path
handles the op. C++: implicit in the switch statement grouping of `ckernel_name()`. -/

/-- The compute family for each CKernelId. -/
def CKernelId.family : CKernelId -> CKernelFamily
  | .OPAQUE => .Opaque
  | .GEMM_MM | .GEMM_BMM | .GEMM_MATMUL | .GEMM_ADDMM
  | .GEMM_LINEAR | .GEMM_ADDBMM | .GEMM_BADDBMM | .GEMM_EINSUM => .LinAlg
  | .CONV1D | .CONV2D | .CONV3D
  | .CONV_TRANSPOSE1D | .CONV_TRANSPOSE2D | .CONV_TRANSPOSE3D => .Conv
  | .SDPA | .MHA | .ROPE | .POSITION_BIAS => .Attention
  | .LAYER_NORM | .BATCH_NORM_TRAIN | .BATCH_NORM_EVAL
  | .GROUP_NORM | .INSTANCE_NORM | .RMS_NORM => .Norm
  | .ACT_RELU | .ACT_GELU | .ACT_SILU | .ACT_SIGMOID | .ACT_TANH
  | .ACT_HARDSWISH | .ACT_LEAKY_RELU | .ACT_ELU | .ACT_SOFTMAX
  | .ACT_LOG_SOFTMAX | .ACT_DROPOUT | .ACT_CLAMP | .ACT_MISH => .Activation
  | .EWISE_ADD | .EWISE_MUL | .EWISE_SUB | .EWISE_DIV | .EWISE_POW
  | .EWISE_MAX | .EWISE_MIN | .EWISE_MOD | .EWISE_WHERE => .EwiseBinary
  | .EWISE_EXP | .EWISE_LOG | .EWISE_SQRT | .EWISE_RSQRT | .EWISE_ABS
  | .EWISE_NEG | .EWISE_SIGN | .EWISE_FLOOR | .EWISE_CAST | .EWISE_FILL => .EwiseUnary
  | .REDUCE_SUM | .REDUCE_MEAN | .REDUCE_MAX | .REDUCE_MIN
  | .REDUCE_ARGMAX | .REDUCE_ARGMIN | .REDUCE_CUMSUM | .REDUCE_TOPK => .Reduction
  | .POOL_MAX1D | .POOL_MAX2D | .POOL_MAX3D | .POOL_AVG1D
  | .POOL_AVG2D | .POOL_AVG3D | .POOL_ADAPTIVE_MAX | .POOL_ADAPTIVE_AVG => .Pooling
  | .VIEW | .RESHAPE | .PERMUTE | .TRANSPOSE | .CONTIGUOUS | .EXPAND
  | .SQUEEZE | .SLICE | .INDEX_SELECT | .INDEX | .SCATTER | .MASKED_FILL
  | .PAD | .CAT | .STACK | .UNFOLD => .DataMovement
  | .EMBEDDING | .EMBEDDING_BAG => .Embedding
  | .COPY_ | .CLONE => .Copy
  | .INTERPOLATE | .GRID_SAMPLE | .IM2COL => .Vision
  | .FUSED_ATTENTION | .FUSED_LINEAR_ACT
  | .FUSED_NORM_LINEAR | .FUSED_SOFTMAX_DROP => .Fused
  | .LINALG_SVD | .LINALG_CHOLESKY | .LINALG_QR | .LINALG_SOLVE
  | .LINALG_EIGH | .LINALG_NORM | .LINALG_CROSS | .CDIST | .FFT => .LinAlgDecomp
  | .ASSOC_SCAN | .SELECTIVE_SCAN | .SSD_CHUNK
  | .WKV_RECURRENCE | .RETENTION | .MLSTM_RECURRENCE => .SSM
  | .DEQUANT_GEMM | .MOE_ROUTE_GEMM | .PAGED_ATTENTION
  | .FUSED_CROSS_ENTROPY | .LINEAR_ATTN_CAUSAL | .RAGGED_ATTN => .ProdInference
  | .GAUSSIAN_RASTERIZE | .HASH_GRID_ENCODE
  | .VOLUME_RENDER | .SH_EVAL => .Rendering3D
  | .FFT_CONV | .MONARCH_MATMUL | .SPMM_GNN
  | .SDDMM_GNN | .SINKHORN => .StructuredGraph
  | .COMM_ALLREDUCE | .COMM_ALLGATHER | .COMM_REDUCE_SCATTER | .COMM_BROADCAST
  | .COMM_ALL_TO_ALL | .COMM_SEND | .COMM_RECV | .COMM_REDUCE
  | .COMM_GATHER | .COMM_SCATTER | .COMM_BARRIER => .Comm
  | .IO_LOAD | .IO_PREFETCH | .IO_CHECKPOINT_SAVE | .IO_CHECKPOINT_LOAD => .IOOp
  | .RNG_UNIFORM | .RNG_NORMAL => .RNG

/-! ## Section classification

C++ layout: Section 1 ordinals 1..99 are frozen (cannot be renumbered without
breaking CDAG_VERSION compatibility). Section 2 ordinals 100+ are the growth zone. -/

/-- Which section of the taxonomy an op belongs to.
    Section 1 ordinals are frozen; Section 2 is the growth zone. -/
inductive Section where
  | opaque    -- ordinal 0, not a real compute op
  | section1  -- ordinals 1..99, frozen since CDAG_VERSION=3
  | section2  -- ordinals 100+, added in CDAG_VERSION=4
  deriving DecidableEq, Repr

/-- Classify a CKernelId into its section based on ordinal. -/
def CKernelId.section (k : CKernelId) : Section :=
  if k.toOrdinal = 0 then .opaque
  else if k.toOrdinal ≤ 99 then .section1
  else .section2

/-! ## Ordinal bounds

Every ordinal fits in uint8_t (< 256). The maximum ordinal is 146.
C++ sentinel NUM_KERNELS = 147. -/

/-- C++ sentinel value. Not a valid CKernelId. -/
def numKernels : Nat := 147

/-- Every CKernelId ordinal is strictly below the sentinel. -/
theorem toOrdinal_lt_numKernels (k : CKernelId) : k.toOrdinal < numKernels := by
  cases k <;> simp [CKernelId.toOrdinal, numKernels]

/-- Every ordinal fits in uint8_t. -/
theorem toOrdinal_lt_256 (k : CKernelId) : k.toOrdinal < 256 := by
  cases k <;> simp [CKernelId.toOrdinal]

/-! ## Exhaustive enumeration

The complete list of all CKernelId values, proved exhaustive and duplicate-free.
Must come before Fintype instance and injectivity proofs that depend on it. -/

/-- Exhaustive list of all 147 CKernelId values in ordinal order. -/
def CKernelId.all : List CKernelId := [
  .OPAQUE,
  -- Section 1
  .GEMM_MM, .GEMM_BMM, .GEMM_MATMUL, .GEMM_ADDMM,
  .GEMM_LINEAR, .GEMM_ADDBMM, .GEMM_BADDBMM, .GEMM_EINSUM,
  .CONV1D, .CONV2D, .CONV3D,
  .CONV_TRANSPOSE1D, .CONV_TRANSPOSE2D, .CONV_TRANSPOSE3D,
  .SDPA, .MHA, .ROPE, .POSITION_BIAS,
  .LAYER_NORM, .BATCH_NORM_TRAIN, .BATCH_NORM_EVAL,
  .GROUP_NORM, .INSTANCE_NORM, .RMS_NORM,
  .ACT_RELU, .ACT_GELU, .ACT_SILU, .ACT_SIGMOID, .ACT_TANH,
  .ACT_HARDSWISH, .ACT_LEAKY_RELU, .ACT_ELU, .ACT_SOFTMAX,
  .ACT_LOG_SOFTMAX, .ACT_DROPOUT, .ACT_CLAMP, .ACT_MISH,
  .EWISE_ADD, .EWISE_MUL, .EWISE_SUB, .EWISE_DIV, .EWISE_POW,
  .EWISE_MAX, .EWISE_MIN, .EWISE_MOD, .EWISE_WHERE,
  .EWISE_EXP, .EWISE_LOG, .EWISE_SQRT, .EWISE_RSQRT, .EWISE_ABS,
  .EWISE_NEG, .EWISE_SIGN, .EWISE_FLOOR, .EWISE_CAST, .EWISE_FILL,
  .REDUCE_SUM, .REDUCE_MEAN, .REDUCE_MAX, .REDUCE_MIN,
  .REDUCE_ARGMAX, .REDUCE_ARGMIN, .REDUCE_CUMSUM, .REDUCE_TOPK,
  .POOL_MAX1D, .POOL_MAX2D, .POOL_MAX3D,
  .POOL_AVG1D, .POOL_AVG2D, .POOL_AVG3D,
  .POOL_ADAPTIVE_MAX, .POOL_ADAPTIVE_AVG,
  .VIEW, .RESHAPE, .PERMUTE, .TRANSPOSE, .CONTIGUOUS, .EXPAND,
  .SQUEEZE, .SLICE, .INDEX_SELECT, .INDEX, .SCATTER, .MASKED_FILL,
  .PAD, .CAT, .STACK, .UNFOLD,
  .EMBEDDING, .EMBEDDING_BAG,
  .COPY_, .CLONE,
  .INTERPOLATE, .GRID_SAMPLE, .IM2COL,
  .FUSED_ATTENTION, .FUSED_LINEAR_ACT,
  .FUSED_NORM_LINEAR, .FUSED_SOFTMAX_DROP,
  -- Section 2
  .LINALG_SVD, .LINALG_CHOLESKY, .LINALG_QR, .LINALG_SOLVE,
  .LINALG_EIGH, .LINALG_NORM, .LINALG_CROSS, .CDIST, .FFT,
  .ASSOC_SCAN, .SELECTIVE_SCAN, .SSD_CHUNK,
  .WKV_RECURRENCE, .RETENTION, .MLSTM_RECURRENCE,
  .DEQUANT_GEMM, .MOE_ROUTE_GEMM, .PAGED_ATTENTION,
  .FUSED_CROSS_ENTROPY, .LINEAR_ATTN_CAUSAL, .RAGGED_ATTN,
  .GAUSSIAN_RASTERIZE, .HASH_GRID_ENCODE, .VOLUME_RENDER, .SH_EVAL,
  .FFT_CONV, .MONARCH_MATMUL, .SPMM_GNN, .SDDMM_GNN, .SINKHORN,
  .COMM_ALLREDUCE, .COMM_ALLGATHER, .COMM_REDUCE_SCATTER,
  .COMM_BROADCAST, .COMM_ALL_TO_ALL, .COMM_SEND, .COMM_RECV,
  .COMM_REDUCE, .COMM_GATHER, .COMM_SCATTER,
  .IO_LOAD, .IO_PREFETCH, .IO_CHECKPOINT_SAVE, .IO_CHECKPOINT_LOAD,
  .RNG_UNIFORM, .RNG_NORMAL,
  .COMM_BARRIER
]

/-- The list is exhaustive: every CKernelId appears in `all`. -/
theorem CKernelId.mem_all (k : CKernelId) : k ∈ CKernelId.all := by
  cases k <;> simp [CKernelId.all]

/-- Total count: 147 values (OPAQUE + 146 ops). Matches C++ NUM_KERNELS. -/
theorem CKernelId.all_length : CKernelId.all.length = numKernels := by native_decide

/-- The list has no duplicates. Combined with exhaustiveness, this proves
    the CKernelId type has exactly 147 inhabitants. -/
theorem CKernelId.all_nodup : CKernelId.all.Nodup := by native_decide

/-! ## Ordinal injectivity

The ordinal assignment is injective: distinct CKernelId constructors
get distinct ordinals. This is the formal statement that the C++ enum
has no value collisions. -/

/-- Fintype instance for CKernelId derived from the exhaustive list. -/
instance : Fintype CKernelId where
  elems := ⟨CKernelId.all, CKernelId.all_nodup⟩
  complete := CKernelId.mem_all

/-- The ordinal list has no duplicates. -/
private theorem ordinals_nodup :
    (CKernelId.all.map CKernelId.toOrdinal).Nodup := by native_decide

/-- toOrdinal is injective: no two distinct CKernelIds share an ordinal.
    Proof strategy: `all.map toOrdinal` is nodup, which by
    `List.inj_on_of_nodup_map` means toOrdinal is injective on `all`.
    Since `all` is exhaustive, this gives global injectivity. -/
theorem toOrdinal_injective (a b : CKernelId) (h : a.toOrdinal = b.toOrdinal) : a = b := by
  exact List.inj_on_of_nodup_map ordinals_nodup
    (CKernelId.mem_all a) (CKernelId.mem_all b) h

/-! ## Section 1 frozen property

Section 1 ops occupy exactly ordinals 1..99 and Section 2 ops occupy 100..146.
This models the C++ invariant: Section 1 ordinals are frozen since CDAG_VERSION=3,
meaning no renumbering is permitted. -/

/-- A CKernelId is in Section 1 iff its ordinal is in [1, 99]. -/
theorem section1_iff (k : CKernelId) :
    k.section = .section1 ↔ 1 ≤ k.toOrdinal ∧ k.toOrdinal ≤ 99 := by
  cases k <;> simp [CKernelId.section, CKernelId.toOrdinal]

/-- A CKernelId is in Section 2 iff its ordinal is in [100, 146]. -/
theorem section2_iff (k : CKernelId) :
    k.section = .section2 ↔ 100 ≤ k.toOrdinal ∧ k.toOrdinal ≤ 146 := by
  cases k <;> simp [CKernelId.section, CKernelId.toOrdinal]

/-- OPAQUE is the only ordinal-0 op. -/
theorem opaque_iff (k : CKernelId) :
    k.section = .opaque ↔ k = .OPAQUE := by
  cases k <;> simp [CKernelId.section, CKernelId.toOrdinal]

/-! ## Section counting

Section 1 has exactly 99 ops, Section 2 has exactly 47 ops. -/

/-- Section 1 ops (ordinals 1..99). -/
def CKernelId.section1Ops : List CKernelId :=
  CKernelId.all.filter (fun k => k.section == .section1)

/-- Section 2 ops (ordinals 100..146). -/
def CKernelId.section2Ops : List CKernelId :=
  CKernelId.all.filter (fun k => k.section == .section2)

/-- Section 1 contains exactly 99 ops. -/
theorem section1_count : CKernelId.section1Ops.length = 99 := by native_decide

/-- Section 2 contains exactly 47 ops. -/
theorem section2_count : CKernelId.section2Ops.length = 47 := by native_decide

/-! ## Family counting

Per-family op counts match the C++ header comments. -/

/-- Filter `all` by family. -/
def CKernelId.ofFamily (f : CKernelFamily) : List CKernelId :=
  CKernelId.all.filter (fun k => k.family == f)

theorem linAlg_count : (CKernelId.ofFamily .LinAlg).length = 8 := by native_decide
theorem conv_count : (CKernelId.ofFamily .Conv).length = 6 := by native_decide
theorem attention_count : (CKernelId.ofFamily .Attention).length = 4 := by native_decide
theorem norm_count : (CKernelId.ofFamily .Norm).length = 6 := by native_decide
theorem activation_count : (CKernelId.ofFamily .Activation).length = 13 := by native_decide
theorem ewiseBinary_count : (CKernelId.ofFamily .EwiseBinary).length = 9 := by native_decide
theorem ewiseUnary_count : (CKernelId.ofFamily .EwiseUnary).length = 10 := by native_decide
theorem reduction_count : (CKernelId.ofFamily .Reduction).length = 8 := by native_decide
theorem pooling_count : (CKernelId.ofFamily .Pooling).length = 8 := by native_decide
theorem dataMovement_count : (CKernelId.ofFamily .DataMovement).length = 16 := by native_decide
theorem embedding_count : (CKernelId.ofFamily .Embedding).length = 2 := by native_decide
theorem copy_count : (CKernelId.ofFamily .Copy).length = 2 := by native_decide
theorem vision_count : (CKernelId.ofFamily .Vision).length = 3 := by native_decide
theorem fused_count : (CKernelId.ofFamily .Fused).length = 4 := by native_decide
theorem linAlgDecomp_count : (CKernelId.ofFamily .LinAlgDecomp).length = 9 := by native_decide
theorem ssm_count : (CKernelId.ofFamily .SSM).length = 6 := by native_decide
theorem prodInference_count : (CKernelId.ofFamily .ProdInference).length = 6 := by native_decide
theorem rendering3D_count : (CKernelId.ofFamily .Rendering3D).length = 4 := by native_decide
theorem structuredGraph_count : (CKernelId.ofFamily .StructuredGraph).length = 5 := by native_decide
theorem comm_count : (CKernelId.ofFamily .Comm).length = 11 := by native_decide
theorem ioOp_count : (CKernelId.ofFamily .IOOp).length = 4 := by native_decide
theorem rng_count : (CKernelId.ofFamily .RNG).length = 2 := by native_decide
theorem opaque_count : (CKernelId.ofFamily .Opaque).length = 1 := by native_decide

/-! ## Registration table model

Models the sorted-array registration table. C++: `CKernelTable` with
`CKERNEL_TABLE_CAP = 256` slots, sorted by SchemaHash, binary search for classify. -/

/-- Maximum registration table capacity. C++: `CKERNEL_TABLE_CAP = 256`. -/
def ckernelTableCap : Nat := 256

/-- A registration entry: schema hash mapped to CKernelId.
    C++: `struct CKernelEntry { SchemaHash schema_hash; CKernelId id; }`. -/
structure CKernelEntry where
  schemaHash : Nat
  id : CKernelId
  deriving DecidableEq, Repr

/-- Registration table. Sorted by schemaHash, binary-searched by classify.
    Invariant: entries are sorted and size <= ckernelTableCap. -/
structure CKernelTable where
  entries : List CKernelEntry
  size : Nat
  hsize : size = entries.length
  hsorted : entries.Pairwise (fun a b => a.schemaHash < b.schemaHash)
  hcap : size ≤ ckernelTableCap

/-- Empty table. Initial state before Vessel registers ops. -/
def CKernelTable.empty : CKernelTable where
  entries := []
  size := 0
  hsize := rfl
  hsorted := List.Pairwise.nil
  hcap := by omega

/-- Classification returns OPAQUE for any hash in an empty table.
    C++: standalone library ships with empty table, all ops are OPAQUE. -/
theorem CKernelTable.classify_empty (h : Nat) :
    ∀ e ∈ CKernelTable.empty.entries, e.schemaHash ≠ h := by
  simp [CKernelTable.empty]

/-- The table capacity (256) exceeds the total op count (147),
    so all ops can be registered with room for multi-registration aliases. -/
theorem table_cap_sufficient : numKernels ≤ ckernelTableCap := by
  simp [numKernels, ckernelTableCap]

end Crucible
