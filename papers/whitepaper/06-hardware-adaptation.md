# 6. Hardware Adaptation

Longitude and Augur are designs; the hardware profiling interfaces they depend on (CUPTI, NVML, rocprofiler) are standard vendor APIs. CostModel.h implements the hardware specification and cost model structures.

## 6.1 Hardware Profiling

CUPTI (NVIDIA) and rocprofiler (AMD) provide per-kernel counters: SM/CU utilization, memory bandwidth saturation, achieved occupancy, tensor core utilization, register spill count, cache hit rates, warp execution efficiency, pipeline stall reasons. NVML and rocm-smi provide device-level metrics: clocks, power, temperature, ECC errors, throttle events, usable memory. Crucible abstracts these behind a vendor-agnostic profiling interface.

## 6.2 Longitude: Startup Calibration

Longitude runs at startup and on topology change (Relay failure, new Relay, sustained drift detected by Augur). It populates the HardwareProfile struct (CostModel.h) with measured values, replacing the nominal presets (B200, H100, MI300X, A100).

**Phase 1: GPU profiling.** Per-GPU, in parallel: GEMM benchmark measures sustained compute throughput (a thermally throttling GPU delivers less than spec). Streaming copy measures actual HBM bandwidth. Clock readings under load reveal true boost frequency. NVML/rocm-smi report power, temperature, ECC errors, usable memory after driver overhead. Output: a calibrated HardwareProfile per device with measured values for all fields (num_sms, smem_per_sm, hbm_bw, peak_fp16, etc.).

**Phase 2: Network probing.** All-pairs: ping-pong latency, unidirectional and bidirectional bandwidth, topology detection (NVSwitch, NVLink, PCIe, InfiniBand, RoCE, TCP). Output: weighted network graph of actual measured connectivity.

**Phase 3: Kernel calibration.** Representative kernels benchmarked at several shapes. Ratio of actual measured time to analytical prediction yields a correction factor per kernel class, absorbing cache behavior, instruction scheduling, TLB pressure. With correction factors, model predictions track measurements within a few percent.

**Phase 4: Configuration optimization.** Given measured profiles and topology, formulate system configuration as SMT constraint optimization. Variables: parallelism degrees (TP, DP, PP, EP, CP), GPU-to-group assignment, communication algorithm per collective per message size, gradient bucket sizes, per-tensor checkpointing decisions, per-op precision assignment, batch size. Constraints: memory limits, communication feasibility, hardware capabilities. Objective: minimize predicted iteration time. Result: complete configuration optimal within the encoded model.

## 6.3 Augur: Continuous Monitoring

Augur runs every iteration, comparing predictions against measurements.

**Digital twin.** From the Merkle DAG, kernel cost predictions (roofline model with correction factors), and the memory plan, Augur predicts each iteration. Per-kernel: execution time, utilization, bottleneck classification (COMPUTE, MEMORY_BW, COMMUNICATION, BUBBLE, IMBALANCE). Per-iteration: critical path through the DAG, forward/backward/optimizer/communication breakdown.

**Drift detection.** Compare predicted vs actual per iteration. Small sustained deviations update correction factors. Large sustained deviations trigger diagnosis: thermal throttling (NVML clock drop), hardware degradation (ECC error increase), network congestion (latency spike), workload change (dynamic shapes). Confirmed hardware change triggers Longitude recalibration.

**Bottleneck identification.** Classify dominant bottleneck, rank optimizations by expected_speedup * confidence. This drives Keeper recommendations.

## 6.4 Model Intelligence

Periodically, Augur computes diagnostics of the Vigil's training dynamics from actual tensor data:

- **Hessian spectrum.** Top eigenvalues via Lanczos iteration (Hessian-vector products, O(N) cost each). Yields smoothness, condition number, convergence rate bounds [Nesterov 1983].
- **Gradient health.** Per-layer gradient norms, signal-to-noise ratio, Jacobian singular values. Detects vanishing/exploding gradients with layer attribution.
- **Representation capacity.** Per-layer effective rank via randomized SVD [Halko et al. 2011]. Identifies overparameterization and dead neurons (near-zero variance).
- **Layer redundancy.** CKA [Kornblith et al. 2019] between adjacent layers. High CKA -> layers compute nearly identical functions -> pruning candidate.
- **Convergence prediction.** Exponential loss curve fit with confidence intervals. Chinchilla scaling law analysis [Hoffmann et al. 2022].

Each diagnostic is a standard technique; Crucible's contribution is integrating them into the runtime to drive automated optimization (Section 8).
