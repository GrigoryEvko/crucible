# 6. Hardware Adaptation

This section describes how Crucible interfaces with hardware through measurement and continuous monitoring. Longitude and Augur are described as designs; the hardware profiling interfaces they depend on (CUPTI, NVML, rocprofiler) are standard vendor APIs.

## 6.1 Hardware Profiling

Each Relay exposes hardware state through vendor profiling APIs. CUPTI (NVIDIA) and rocprofiler (AMD) provide per-kernel counters: SM/CU utilization, memory bandwidth saturation, achieved occupancy, tensor core utilization, register spill count, cache hit rates, warp execution efficiency, and pipeline stall reasons. NVML and rocm-smi provide device-level metrics: clock speeds, power draw, temperature, ECC error counts, throttle events, and usable memory.

Crucible abstracts these behind a vendor-agnostic profiling interface so that all higher layers (Longitude, Augur, the Keeper) operate on uniform metrics regardless of hardware vendor.

## 6.2 Longitude: Startup Calibration

Longitude runs once at startup and again when the Canopy topology changes (Relay failure, new Relay, sustained performance drift detected by Augur). Its purpose is to measure actual hardware characteristics and solve for optimal system configuration.

**Phase 1: GPU profiling.** Per-GPU, in parallel across all Relays: a GEMM benchmark measures actual sustained compute throughput (a thermally throttling GPU delivers less than its specification). Streaming copy measures actual memory bandwidth. Clock readings under sustained load reveal true boost frequency. NVML/rocm-smi queries report power, temperature, ECC errors, and usable memory after driver overhead. The output is a measured GPU profile per device.

**Phase 2: Network probing.** All-pairs, in parallel: ping-pong latency measurement, unidirectional bandwidth measurement, bidirectional bandwidth measurement, and topology detection (NVSwitch, NVLink, PCIe, InfiniBand, RoCE, TCP). The output is a complete weighted network graph representing actual measured connectivity.

**Phase 3: Kernel calibration.** Representative kernels (selected by predicted execution time from the cost model) are benchmarked at several shapes. Comparing actual measured time to the analytical model's prediction yields a correction factor per kernel class. These factors absorb hardware effects not captured by the roofline model: cache behavior, instruction scheduling, TLB pressure. After calibration, model predictions with correction factors applied are expected to closely track actual measurements.

**Phase 4: Configuration optimization.** Given measured hardware profiles and network topology, Crucible formulates the system configuration as a constraint optimization problem. Decision variables include parallelism degrees (TP, DP, PP, EP, CP), GPU-to-group assignment, communication algorithm per collective per message size, gradient bucket sizes, activation checkpointing decisions (per-tensor: store vs recompute), mixed precision assignment (per-op), and batch size. Constraints enforce memory limits, communication feasibility, and hardware capabilities. The objective minimizes predicted iteration time. The result is a complete configuration proved optimal within the encoded model.

## 6.3 Augur: Continuous Monitoring

Augur runs every iteration, comparing predictions against measurements and maintaining a model of the Vigil's training dynamics.

**Digital twin.** From the Merkle DAG, FX kernel predictions, and Longitude correction factors, Augur constructs a predictive model of each iteration. Per-kernel: predicted execution time, utilization, bottleneck classification (compute-bound, memory-bandwidth-bound, communication-bound, pipeline-bubble, load-imbalance). Per-iteration: critical path through the DAG, forward/backward/optimizer/communication breakdown. Memory timeline is exact --- it is the memory plan itself.

**Drift detection.** Each iteration: compare predicted vs actual time. Small sustained deviations trigger correction factor updates. Large sustained deviations trigger diagnosis: thermal throttling (clock drop via NVML), hardware degradation (ECC error increase), network congestion (latency spike), workload change (dynamic shapes triggered re-planning). If hardware change is confirmed, Longitude recalibrates.

**Bottleneck identification.** Augur classifies the dominant bottleneck and identifies which specific optimization would have the highest impact. This is the basis for the recommendations that the Keeper may act on.

## 6.4 Model Intelligence

Periodically (not every iteration), Augur computes diagnostics of the Vigil's training dynamics using the actual tensor data flowing through the recorded execution:

**Hessian spectrum.** Top eigenvalues via Lanczos iteration (Hessian-vector products, each costing one backward pass). Yields smoothness, strong convexity, condition number, and convergence rate bounds [Nesterov 1983].

**Gradient health.** Per-layer gradient norms, signal-to-noise ratio, Jacobian singular values. Detects vanishing and exploding gradients with specific layer attribution.

**Representation capacity.** Per-layer effective rank via randomized SVD [Halko et al. 2011]. Identifies overparameterization (effective rank much smaller than hidden dimension) and dead neurons (near-zero variance).

**Layer redundancy.** Centered Kernel Alignment (CKA) [Kornblith et al. 2019] between adjacent layers. High CKA indicates that two layers compute nearly identical functions, suggesting one could be pruned.

**Convergence prediction.** Exponential fit to the loss curve with confidence intervals. Scaling law analysis via Chinchilla fit [Hoffmann et al. 2022] relating model size, data size, and compute budget to predicted loss.

These diagnostics feed into the model-aware optimizations described in Section 8. Each diagnostic is a standard technique from the ML literature; Crucible's contribution is integrating them into the runtime where they can drive automated optimization decisions.
