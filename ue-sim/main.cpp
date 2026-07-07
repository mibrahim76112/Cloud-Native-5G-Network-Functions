#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <future>

#include <grpcpp/grpcpp.h>
#include "n1.grpc.pb.h"
#include "n4.grpc.pb.h"

struct RpcResult {
    bool    success;
    int64_t latency_us;
};

class UESimulator {
public:
    UESimulator(const std::string& amf_addr, const std::string& upf_addr) {
        amf_stub_ = n1::N1Service::NewStub(
            grpc::CreateChannel(amf_addr, grpc::InsecureChannelCredentials()));
        upf_stub_ = n4::N4Service::NewStub(
            grpc::CreateChannel(upf_addr, grpc::InsecureChannelCredentials()));
    }

    RpcResult register_ue(const std::string& supi) {
        n1::RegistrationRequest req;
        req.set_supi(supi);
        req.set_registration_type("initial");
        req.set_requested_nssai("sst:1,sd:000001");

        n1::RegistrationResponse resp;
        grpc::ClientContext ctx;

        auto t0 = std::chrono::high_resolution_clock::now();
        auto status = amf_stub_->Register(&ctx, req, &resp);
        auto t1 = std::chrono::high_resolution_clock::now();

        int64_t lat = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        return { status.ok() && resp.accepted(), lat };
    }

    RpcResult classify_packet(const std::string& src_ip, const std::string& session_id) {
        n4::PacketClassifyRequest req;
        req.set_src_ip(src_ip);
        req.set_dst_ip("8.8.8.8");
        req.set_src_port(54321);
        req.set_dst_port(443);
        req.set_protocol("TCP");
        req.set_session_id(session_id);

        n4::PacketClassifyResponse resp;
        grpc::ClientContext ctx;

        auto t0 = std::chrono::high_resolution_clock::now();
        auto status = upf_stub_->ClassifyPacket(&ctx, req, &resp);
        auto t1 = std::chrono::high_resolution_clock::now();

        int64_t lat = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        return { status.ok(), lat };
    }

private:
    std::unique_ptr<n1::N1Service::Stub> amf_stub_;
    std::unique_ptr<n4::N4Service::Stub> upf_stub_;
};

struct Stats {
    double mean_us;
    double p50_us;
    double p95_us;
    double p99_us;
    double min_us;
    double max_us;
    double throughput_rps;
};

Stats compute_stats(std::vector<int64_t>& latencies, double elapsed_sec) {
    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0LL);
    Stats s;
    s.mean_us        = sum / n;
    s.min_us         = latencies.front();
    s.max_us         = latencies.back();
    s.p50_us         = latencies[n * 50 / 100];
    s.p95_us         = latencies[n * 95 / 100];
    s.p99_us         = latencies[n * 99 / 100];
    s.throughput_rps = n / elapsed_sec;
    return s;
}

void print_stats(const std::string& label, const Stats& s) {
    std::cout << "\n── " << label << " ───────────────────────────────\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Throughput : " << s.throughput_rps << " req/sec\n";
    std::cout << "  Mean       : " << s.mean_us        << " µs\n";
    std::cout << "  Min        : " << s.min_us         << " µs\n";
    std::cout << "  P50        : " << s.p50_us         << " µs\n";
    std::cout << "  P95        : " << s.p95_us         << " µs\n";
    std::cout << "  P99        : " << s.p99_us         << " µs\n";
    std::cout << "  Max        : " << s.max_us         << " µs\n";
}

// ── Sequential benchmark ──────────────────────────────────────────────────
Stats run_sequential(UESimulator& sim, int num_ues) {
    std::cout << "\n[BENCH] Sequential registration (" << num_ues << " UEs)...\n";
    std::vector<int64_t> latencies;
    int success = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_ues; i++) {
        std::string supi = "imsi-00101000000" + std::to_string(10000 + i);
        auto r = sim.register_ue(supi);
        latencies.push_back(r.latency_us);
        if (r.success) success++;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "  Successful: " << success << "/" << num_ues << "\n";
    return compute_stats(latencies, elapsed);
}

// ── Concurrent benchmark ──────────────────────────────────────────────────
Stats run_concurrent(const std::string& amf_addr, const std::string& upf_addr,
                     int num_ues, int num_threads) {
    std::cout << "\n[BENCH] Concurrent registration (" << num_ues
              << " UEs, " << num_threads << " threads)...\n";

    std::vector<int64_t> latencies;
    std::mutex           lat_mutex;
    std::atomic<int>     success{0};

    int ues_per_thread = num_ues / num_threads;

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    for (int t = 0; t < num_threads; t++) {
        int start_idx = t * ues_per_thread;
        futures.push_back(std::async(std::launch::async,
            [&, start_idx]() {
                // Each thread gets its own stub (gRPC channels are thread-safe
                // but stubs are not — create per thread)
                UESimulator sim(amf_addr, upf_addr);
                for (int i = start_idx; i < start_idx + ues_per_thread; i++) {
                    std::string supi = "imsi-99901000000" + std::to_string(10000 + i);
                    auto r = sim.register_ue(supi);
                    {
                        std::lock_guard<std::mutex> lock(lat_mutex);
                        latencies.push_back(r.latency_us);
                    }
                    if (r.success) success++;
                }
            }));
    }

    for (auto& f : futures) f.wait();

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "  Successful: " << success << "/" << num_ues << "\n";
    return compute_stats(latencies, elapsed);
}

// ── Packet classification benchmark ──────────────────────────────────────
Stats run_classification(UESimulator& sim, int num_packets) {
    std::cout << "\n[BENCH] Packet classification (" << num_packets << " packets)...\n";
    std::vector<int64_t> latencies;
    int success = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_packets; i++) {
        std::string src_ip = "10.0.0." + std::to_string((i % 254) + 1);
        auto r = sim.classify_packet(src_ip, "sess-" + std::to_string((i % 50) + 1));
        latencies.push_back(r.latency_us);
        if (r.success) success++;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "  Successful: " << success << "/" << num_packets << "\n";
    return compute_stats(latencies, elapsed);
}

int main() {
    const char* amf_env      = std::getenv("AMF_ADDRESS");
    const char* upf_env      = std::getenv("UPF_ADDRESS");
    const char* n_env        = std::getenv("NUM_UES");
    const char* threads_env  = std::getenv("BENCH_THREADS");
    const char* pkts_env     = std::getenv("NUM_PACKETS");

    std::string amf_addr    = amf_env     ? amf_env     : "localhost:50051";
    std::string upf_addr    = upf_env     ? upf_env     : "localhost:50052";
    int         num_ues     = n_env       ? std::stoi(n_env)       : 100;
    int         num_threads = threads_env ? std::stoi(threads_env) : 4;
    int         num_packets = pkts_env    ? std::stoi(pkts_env)    : 1000;

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G UE Simulator + Benchmark       ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "AMF      : " << amf_addr    << "\n";
    std::cout << "UPF      : " << upf_addr    << "\n";
    std::cout << "UEs      : " << num_ues     << "\n";
    std::cout << "Threads  : " << num_threads << "\n";
    std::cout << "Packets  : " << num_packets << "\n\n";

    std::cout << "Waiting 2s for NFs to be ready...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    UESimulator sim(amf_addr, upf_addr);

    // Run sequential baseline
    auto seq_stats = run_sequential(sim, num_ues);
    print_stats("Sequential Registration (N1)", seq_stats);

    // Run concurrent load test
    auto con_stats = run_concurrent(amf_addr, upf_addr, num_ues, num_threads);
    print_stats("Concurrent Registration (N1, " +
                std::to_string(num_threads) + " threads)", con_stats);

    // Packet classification
    auto pkt_stats = run_classification(sim, num_packets);
    print_stats("Packet Classification (N4)", pkt_stats);

    // Summary
    std::cout << "\n══════════════════════════════════════════════════\n";
    std::cout << "  BENCHMARK SUMMARY\n";
    std::cout << "══════════════════════════════════════════════════\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Sequential reg throughput  : "
              << seq_stats.throughput_rps  << " reg/sec\n";
    std::cout << "  Concurrent reg throughput  : "
              << con_stats.throughput_rps  << " reg/sec  ("
              << num_threads << " threads)\n";
    std::cout << "  Throughput gain            : "
              << std::setprecision(1)
              << (con_stats.throughput_rps / seq_stats.throughput_rps)
              << "x\n";
    std::cout << "  Classification throughput  : "
              << std::setprecision(0)
              << pkt_stats.throughput_rps  << " pkt/sec\n";
    std::cout << "  Classification P99 latency : "
              << pkt_stats.p99_us          << " µs\n";
    std::cout << "══════════════════════════════════════════════════\n\n";

    return 0;
}
