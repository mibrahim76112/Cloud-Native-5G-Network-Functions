# 5G Cloud-Native Network Functions

A C++17 implementation of two 5G core Network Functions — AMF and UPF — communicating over gRPC, containerized with Docker, and orchestrated on Kubernetes. Built to mirror the architecture used in real 5G core products at companies like Nokia and Ericsson.

---

## Architecture

```
[ UE Simulator ] --N1/gRPC--> [ AMF :50051 ] --N4/gRPC--> [ UPF :50052 ]
                               |                                          |
                               +-------- 5g-core bridge network ----------+
                                         (Docker / Kubernetes)
```

The UE Simulator sends registration requests to the AMF over the N1 interface. The AMF validates each UE identity (SUPI/NGAP ID), then calls the UPF over N4 to establish a PDU session and assign an IP. The UPF handles all subsequent packet classification — inspecting source/destination IP, port, and protocol to apply 3GPP QoS profiles (voice, video, best-effort) and return a forwarding decision with next-hop resolution. All three services run as isolated containers on a private bridge network, with Docker DNS resolving service names automatically. On Kubernetes, AMF and UPF run as Deployments with ClusterIP Services for discovery, and the UE simulator runs as a Job that benchmarks the system and exits.

The codebase uses gRPC over Protocol Buffers for inter-NF communication — the same transport used in real 5G vendor stacks — with interface names (N1, N4), message types (SUPI, NGAP ID, PDU session, DNN, QoS profile), and procedures modelled directly from 3GPP TS 23.501 and TS 29.244. Configuration is fully environment-variable-driven (12-factor app style). Docker images use multi-stage builds to keep runtime containers minimal.

---

## Benchmark Results

**Docker Compose — 50 UEs, 500 packets:**

| Metric | Registration (N1) | Packet Classification (N4) |
|---|---|---|
| Throughput | 50 reg/sec | 706 pkt/sec |
| P50 Latency | 3.4 ms | 1.3 ms |
| P95 Latency | 5.4 ms | 2.1 ms |
| P99 Latency | 831 ms* | 3.0 ms |

> *P99 spike caused by gRPC cold-start on first connection. P50 of 3.4ms reflects steady-state performance.

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

**Stack:** C++17 · gRPC v1.60 · Protocol Buffers · CMake · Docker · Kubernetes
**3GPP Interfaces:** N1 (UE↔AMF) · N4 (AMF↔UPF)
**References:** 3GPP TS 23.501 · 3GPP TS 29.244 · gRPC C++ Docs