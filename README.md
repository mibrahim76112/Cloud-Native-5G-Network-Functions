# 5G Cloud-Native Network Functions

A C++ implementation of two 5G core Network Functions — AMF and UPF — communicating over gRPC, containerized with Docker, and orchestrated on Kubernetes. Built as a performance benchmarking platform to evaluate transport protocols, thread-pool scaling, and observability patterns for COTS hardware deployment, mirroring engineering decisions made in real 5G core products at companies like Nokia and Ericsson.

---

## Architecture

```
[ UE Simulator ] --N1/gRPC--> [ AMF :50051 ] --N4/gRPC--> [ UPF :50052 ]
                               |                                          |
                               +-------- 5g-core bridge network ----------+
                                         (Docker / Kubernetes)

Observability:
  AMF :9091/metrics  →  Prometheus scrape
  UPF :9092/metrics  →  Prometheus scrape

Transport Benchmark:
  gRPC AMF :50051  vs  REST AMF :8080
```

The UE Simulator sends registration requests to the AMF over the N1 interface. The AMF validates each UE identity (SUPI/NGAP ID), then calls the UPF over N4 to establish a PDU session and assign an IP. The UPF handles all subsequent packet classification — inspecting source/destination IP, port, and protocol to apply 3GPP QoS profiles (voice, video, best-effort) and return a forwarding decision with next-hop resolution.

The UPF uses a configurable thread pool for concurrent packet classification, controlled via the UPF_THREADS environment variable. Both NFs export live Prometheus metrics on dedicated HTTP endpoints, enabling real-time monitoring and Kubernetes-native autoscaling. A separate transport benchmark compares gRPC (protobuf/HTTP2) against REST (JSON/HTTP) on the AMF registration interface, reflecting the 3GPP SBA architectural debate in real 5G deployments.

---

## Key Engineering Decisions

- **gRPC over Protocol Buffers** for inter-NF communication — same transport used in real 5G vendor stacks, with REST alternative benchmarked for comparison
- **Configurable thread pool in UPF** — environment-variable-driven worker count enables performance tuning per COTS hardware profile without recompilation
- **Environment-variable-driven config** (12-factor app style) — AMF_PORT, UPF_ADDRESS, UPF_THREADS, NUM_UES all runtime-configurable
- **Multi-stage Docker builds** — separate build and runtime images minimize container size
- **Prometheus metrics export** — standard text exposition format scrapeable by Kubernetes for autoscaling and alerting
- **Kubernetes manifests** — AMF and UPF as Deployments with ClusterIP Services, UE simulator as a Job

---

## Benchmark Results

### Thread Pool Scaling — UPF Packet Classification

| Threads | Throughput | P99 Latency | Notes |
|---|---|---|---|
| 0 (single-threaded) | 3,260 pkt/sec | 0.7 ms | Lowest overhead for small payloads |
| 4 threads | 2,409 pkt/sec | 1.25 ms | Better under high concurrent load |
| 8 threads | 2,565 pkt/sec | 1.07 ms | Diminishing returns vs 4 threads |

> Single-threaded classification is faster for small isolated payloads due to zero thread synchronization overhead. Thread pool advantage emerges under sustained concurrent load where gRPC completion queues saturate a single worker.

### Concurrent Load — AMF Registration

| Mode | Throughput | P50 Latency | P99 Latency |
|---|---|---|---|
| Sequential (1 thread) | 338 reg/sec | 1.7 ms | 64.5 ms |
| Concurrent (4 threads) | 859 reg/sec | 4.4 ms | 6.9 ms |
| Concurrent (8 threads) | 4,114 reg/sec | 1.6 ms | 5.9 ms |

Concurrent load shows **4.1x throughput gain** with 8 threads. P99 tail latency also improves under concurrent load because requests no longer queue behind slow N4 round trips.

### Transport Comparison — gRPC vs REST (AMF Registration)

| Transport | Throughput | P50 Latency | P99 Latency |
|---|---|---|---|
| gRPC (Protobuf/HTTP2) | 531 req/sec | 1.55 ms | 2.46 ms |
| REST (JSON/HTTP) | 2,228 req/sec | 0.26 ms | 1.85 ms |

> REST appears 4x faster in isolation because the REST AMF stub has no outbound N4 call — it stores context and returns immediately. The gRPC AMF makes a full N4 round trip to UPF per registration. This reflects a real architectural tradeoff: REST is appropriate for stateless SBA interfaces; gRPC's advantage emerges in multi-hop workflows where protobuf serialization efficiency compounds across service calls.

### Docker Compose vs Kubernetes

| Deployment | Registration Throughput | Classification Throughput | P50 Latency |
|---|---|---|---|
| Docker Compose | 50 reg/sec | 706 pkt/sec | 3.4 ms |
| Kubernetes (minikube) | 209 reg/sec | 485 pkt/sec | 3.0 ms |

Registration throughput improved **4x on Kubernetes** due to optimized CNI networking.

### Prometheus Metrics (live scrape output)

```
# AMF :9091/metrics
amf_registrations_total 98
amf_deregistrations_total 0
amf_active_ues 98

# UPF :9092/metrics
upf_packets_classified_total 1000
upf_packets_forwarded_total 376
upf_packets_dropped_total 624
upf_active_sessions 98
upf_thread_pool_size 4
```

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

### Option 3 — Local dev container (with thread pool + metrics)

Open in VSCode → Reopen in Container, then:

```bash
mkdir build && cd build && cmake .. && make -j$(nproc)

# Terminal 1 — UPF with 4 worker threads
UPF_PORT=50052 UPF_THREADS=4 UPF_METRICS_PORT=9092 /workspace/build/upf/upf_server

# Terminal 2 — AMF with metrics
UPF_ADDRESS=localhost:50052 AMF_METRICS_PORT=9091 /workspace/build/amf/amf_server

# Terminal 3 — Benchmark
AMF_ADDRESS=localhost:50051 UPF_ADDRESS=localhost:50052 \
NUM_UES=100 BENCH_THREADS=4 NUM_PACKETS=1000 \
/workspace/build/ue-sim/ue_sim

# Terminal 4 — Scrape metrics
curl http://localhost:9091/metrics
curl http://localhost:9092/metrics
```

### Option 4 — Transport benchmark (gRPC vs REST)

```bash
# Terminal 1 — gRPC AMF
UPF_ADDRESS=localhost:50052 /workspace/build/amf/amf_server

# Terminal 2 — REST AMF
REST_AMF_PORT=8080 /workspace/build/transport-bench/rest_amf_server

# Terminal 3 — Run comparison
GRPC_AMF_ADDRESS=localhost:50051 REST_AMF_HOST=localhost \
REST_AMF_PORT=8080 NUM_REQUESTS=200 \
/workspace/build/transport-bench/bench_compare
```

---

**Stack:** C++17 · gRPC v1.60 · Protocol Buffers · CMake · Docker · Kubernetes · Prometheus · cpp-httplib
**3GPP Interfaces:** N1 (UE↔AMF) · N4 (AMF↔UPF)
**References:** 3GPP TS 23.501 · 3GPP TS 29.244 · 3GPP TS 29.518 · gRPC C++ Docs
