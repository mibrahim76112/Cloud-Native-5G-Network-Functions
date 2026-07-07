# 5G Cloud-Native Network Functions

A C++17 implementation of two 5G core Network Functions — AMF and UPF — communicating over gRPC, containerized with Docker, and orchestrated on Kubernetes. Built to mirror the architecture used in real 5G core products at companies like Nokia and Ericsson.

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
- **Kubernetes manifests** deploy AMF and UPF as Deployments with Services for DNS-based discovery, and UE simulator as a Job that runs once and exits

---

## Benchmark Results

**Docker Compose — 50 UEs, 500 packets:**

| Metric | Registration (N1) | Packet Classification (N4) |
|---|---|---|
| Throughput | 50 reg/sec | 706 pkt/sec |
| P50 Latency | 3.4 ms | 1.3 ms |
| P95 Latency | 5.4 ms | 2.1 ms |
| P99 Latency | 831 ms* | 3.0 ms |

> *P99 registration spike caused by gRPC cold-start channel initialization on first connection.

**Kubernetes (minikube) — 50 UEs, 500 packets:**

| Metric | Registration (N1) | Packet Classification (N4) |
|---|---|---|
| Throughput | 209 reg/sec | 485 pkt/sec |
| P50 Latency | 3.0 ms | 1.3 ms |
| P95 Latency | 9.8 ms | 3.0 ms |
| P99 Latency | 60.9 ms | 25.3 ms |
| Min Latency | 2.2 ms | 0.6 ms |

Registration throughput improved **4x** on Kubernetes vs Docker Compose due to optimized CNI networking.

---

## How To Run

### Option 1 — Docker Compose

```bash
docker-compose up --build
```

### Option 2 — Kubernetes (minikube)

```bash
minikube start
minikube image load cloud-native5gnetworkfunctions-upf:latest
minikube image load cloud-native5gnetworkfunctions-amf:latest
minikube image load cloud-native5gnetworkfunctions-ue-sim:latest

kubectl apply -f k8s/upf-deployment.yaml
kubectl apply -f k8s/amf-deployment.yaml
kubectl apply -f k8s/ue-sim-job.yaml

kubectl get pods
kubectl logs <ue-sim-pod-name>
```

### Option 3 — Local dev container

Open in VSCode → Reopen in Container, then:

```bash
mkdir build && cd build && cmake .. && make -j$(nproc)

# Terminal 1
/workspace/build/upf/upf_server

# Terminal 2
UPF_ADDRESS=localhost:50052 /workspace/build/amf/amf_server

# Terminal 3
AMF_ADDRESS=localhost:50051 UPF_ADDRESS=localhost:50052 NUM_UES=50 /workspace/build/ue-sim/ue_sim
```

---

## Roadmap

- [x] C++17 AMF + UPF gRPC services with N1/N4 interfaces
- [x] UE simulator with P50/P95/P99 latency benchmarking
- [x] Dev container setup (VSCode + Docker)
- [x] Docker multi-stage builds
- [x] Docker Compose multi-NF deployment
- [x] Kubernetes manifests on minikube
- [x] Benchmark results across Docker Compose and Kubernetes
- [ ] Unit tests with gtest
- [ ] Extended benchmark (500 UEs)

---

**Stack:** C++17 · gRPC v1.60 · Protocol Buffers · CMake · Docker · Kubernetes
**3GPP Interfaces:** N1 (UE↔AMF) · N4 (AMF↔UPF)
**References:** 3GPP TS 23.501 · 3GPP TS 29.244 · gRPC C++ Docs
