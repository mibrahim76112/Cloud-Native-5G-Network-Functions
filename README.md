# 5G Cloud-Native Network Functions

> 🚧 **Project in progress** — core NF implementation complete, Docker + Kubernetes ongoing.

A C++17 implementation of two 5G core Network Functions communicating over gRPC, containerized with Docker, and deployed on Kubernetes. Built to mirror the architecture used in real 5G core products at companies like Nokia and Ericsson.

---

## What It Does

The system simulates a simplified 5G core with three components talking to each other over gRPC:

- **AMF** (Access & Mobility Management Function) — handles UE registration over the N1 interface, validates subscriber identities (SUPI/NGAP IDs), and orchestrates PDU session establishment with the UPF
- **UPF** (User Plane Function) — manages PDU sessions over the N4 interface and classifies packets in real time based on 3GPP QoS profiles (voice, video, best-effort), resolving forwarding decisions and next-hop routing
- **UE Simulator** — simulates hundreds of phones registering and sending traffic simultaneously, benchmarking end-to-end latency (P50/P95/P99) and throughput across both interfaces

---

## Key Design Decisions

- **gRPC over Protocol Buffers** for inter-NF communication — same transport used in real 5G vendor stacks
- **Environment-variable-driven config** (12-factor app style) — AMF_PORT, UPF_ADDRESS, NUM_UES all configurable at runtime
- **Multi-stage Docker builds** — separate build and runtime images to minimize container size
- **Docker Compose** brings all three services up on an isolated bridge network with health checks and dependency ordering so UPF is always ready before AMF connects
- **Kubernetes manifests** target minikube for local orchestration testing

---

## Benchmark Results

> Full 500-UE results pending Docker Compose integration.

**Local dev container — initial test (10 UEs):**

| Metric | Registration (N1) | Packet Classification (N4) |
|---|---|---|
| Throughput | 76 reg/sec | 2,088 pkt/sec |
| P50 Latency | 2.8 ms | 0.43 ms |
| P99 Latency | 106 ms | 3.6 ms |

---

## Roadmap

- [x] C++17 AMF + UPF gRPC services with N1/N4 interfaces
- [x] UE simulator with P50/P95/P99 latency benchmarking
- [x] Dev container setup (VSCode + Docker)
- [ ] Docker multi-stage builds + docker-compose deployment
- [ ] Full benchmark with 500 UEs
- [ ] Kubernetes manifests on minikube
- [ ] Unit tests with gtest

---

**Stack:** C++17 · gRPC v1.60 · Protocol Buffers · CMake · Docker · Kubernetes  
**3GPP Interfaces:** N1 (UE↔AMF) · N4 (AMF↔UPF)  
**References:** 3GPP TS 23.501 · 3GPP TS 29.244 · gRPC C++ Docs
