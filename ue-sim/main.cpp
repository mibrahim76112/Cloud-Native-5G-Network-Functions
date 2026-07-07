#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <thread>
#include <iomanip>

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

int main() {
    const char* amf_env = std::getenv("AMF_ADDRESS");
    const char* upf_env = std::getenv("UPF_ADDRESS");
    const char* n_env   = std::getenv("NUM_UES");

    std::string amf_addr = amf_env ? amf_env : "localhost:50051";
    std::string upf_addr = upf_env ? upf_env : "localhost:50052";
    int         num_ues  = n_env   ? std::stoi(n_env) : 200;

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G UE Simulator + Benchmark       ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "AMF: " << amf_addr << "  UPF: " << upf_addr << "\n";
    std::cout << "UEs to simulate: " << num_ues << "\n\n";

    std::cout << "Waiting 2s for NFs to be ready...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    UESimulator sim(amf_addr, upf_addr);

    // Benchmark 1: Registration
    std::cout << "\n[BENCH] Running registration benchmark (" << num_ues << " UEs)...\n";
    std::vector<int64_t> reg_latencies;
    int reg_success = 0;

    auto bench_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_ues; i++) {
        std::string supi = "imsi-00101000000" + std::to_string(10000 + i);
        auto result = sim.register_ue(supi);
        reg_latencies.push_back(result.latency_us);
        if (result.success) reg_success++;
    }
    auto bench_end = std::chrono::high_resolution_clock::now();
    double reg_elapsed = std::chrono::duration<double>(bench_end - bench_start).count();

    std::cout << "  Successful: " << reg_success << "/" << num_ues << "\n";
    auto reg_stats = compute_stats(reg_latencies, reg_elapsed);
    print_stats("Registration (N1) Latency", reg_stats);

    // Benchmark 2: Packet Classification
    int pkt_count = num_ues * 10;
    std::cout << "\n[BENCH] Running packet classification (" << pkt_count << " packets)...\n";
    std::vector<int64_t> pkt_latencies;
    int pkt_success = 0;

    bench_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < pkt_count; i++) {
        std::string src_ip = "10.0.0." + std::to_string((i % 254) + 1);
        auto result = sim.classify_packet(src_ip, "sess-" + std::to_string((i % num_ues) + 1));
        pkt_latencies.push_back(result.latency_us);
        if (result.success) pkt_success++;
    }
    bench_end = std::chrono::high_resolution_clock::now();
    double pkt_elapsed = std::chrono::duration<double>(bench_end - bench_start).count();

    std::cout << "  Successful: " << pkt_success << "/" << pkt_count << "\n";
    auto pkt_stats = compute_stats(pkt_latencies, pkt_elapsed);
    print_stats("Packet Classification (N4) Latency", pkt_stats);

    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  BENCHMARK SUMMARY\n";
    std::cout << "══════════════════════════════════════════\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Registration throughput   : " << reg_stats.throughput_rps << " reg/sec\n";
    std::cout << "  Registration P99 latency  : " << reg_stats.p99_us         << " µs\n";
    std::cout << "  Classification throughput : " << pkt_stats.throughput_rps << " pkt/sec\n";
    std::cout << "  Classification P99 latency: " << pkt_stats.p99_us         << " µs\n";
    std::cout << "══════════════════════════════════════════\n\n";

    return 0;
}
