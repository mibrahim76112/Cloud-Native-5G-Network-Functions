// Transport Benchmark: gRPC vs REST
// Compares registration latency and throughput between:
//   - gRPC (binary protocol, HTTP/2, protobuf)
//   - REST (JSON over HTTP, 3GPP SBA standard interface)

#include "httplib.h"
#include "../build/proto_gen/n1.grpc.pb.h"
#include "../build/proto_gen/n1.pb.h"

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <sstream>

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

struct Result {
    bool    success;
    int64_t latency_us;
};

struct Stats {
    double mean_us, p50_us, p95_us, p99_us, min_us, max_us, throughput;
};

Stats compute(std::vector<int64_t>& v, double elapsed) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    double sum = std::accumulate(v.begin(), v.end(), 0LL);
    return { sum/n, (double)v[n*50/100], (double)v[n*95/100],
             (double)v[n*99/100], (double)v.front(),
             (double)v.back(), n/elapsed };
}

void print_stats(const std::string& label, const Stats& s) {
    std::cout << "\n── " << label << " ───────────────────────────\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Throughput : " << s.throughput << " req/sec\n";
    std::cout << "  Mean       : " << s.mean_us    << " µs\n";
    std::cout << "  P50        : " << s.p50_us     << " µs\n";
    std::cout << "  P95        : " << s.p95_us     << " µs\n";
    std::cout << "  P99        : " << s.p99_us     << " µs\n";
    std::cout << "  Min        : " << s.min_us     << " µs\n";
    std::cout << "  Max        : " << s.max_us     << " µs\n";
}

// ── gRPC benchmark ────────────────────────────────────────────────────────
Stats bench_grpc(const std::string& addr, int n) {
    auto stub = n1::N1Service::NewStub(
        grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

    std::vector<int64_t> lats;
    int success = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; i++) {
        n1::RegistrationRequest req;
        req.set_supi("imsi-grpc-" + std::to_string(i));
        req.set_registration_type("initial");
        req.set_requested_nssai("sst:1");

        n1::RegistrationResponse resp;
        grpc::ClientContext ctx;

        auto t1 = std::chrono::high_resolution_clock::now();
        auto status = stub->Register(&ctx, req, &resp);
        auto t2 = std::chrono::high_resolution_clock::now();

        lats.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t2-t1).count());
        if (status.ok() && resp.accepted()) success++;
    }
    auto tend = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(tend - t0).count();

    std::cout << "  gRPC successful: " << success << "/" << n << "\n";
    return compute(lats, elapsed);
}

// ── REST benchmark ────────────────────────────────────────────────────────
Stats bench_rest(const std::string& host, int port, int n) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    std::vector<int64_t> lats;
    int success = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; i++) {
        std::string supi = "imsi-rest-" + std::to_string(i);
        std::string path = "/namf-comm/v1/ue-contexts/" + supi + "/register";
        std::string body = "{\"registration_type\":\"initial\","
                           "\"requested_nssai\":\"sst:1\"}";

        auto t1 = std::chrono::high_resolution_clock::now();
        auto res = cli.Post(path, body, "application/json");
        auto t2 = std::chrono::high_resolution_clock::now();

        lats.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t2-t1).count());
        if (res && (res->status == 200 || res->status == 201)) success++;
    }
    auto tend = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(tend - t0).count();

    std::cout << "  REST successful: " << success << "/" << n << "\n";
    return compute(lats, elapsed);
}

int main() {
    const char* grpc_env = std::getenv("GRPC_AMF_ADDRESS");
    const char* rest_env = std::getenv("REST_AMF_HOST");
    const char* port_env = std::getenv("REST_AMF_PORT");
    const char* n_env    = std::getenv("NUM_REQUESTS");

    std::string grpc_addr = grpc_env ? grpc_env : "localhost:50051";
    std::string rest_host = rest_env ? rest_env : "localhost";
    int         rest_port = port_env ? std::stoi(port_env) : 8080;
    int         n         = n_env    ? std::stoi(n_env)    : 200;

    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║   Transport Benchmark: gRPC vs REST     ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
    std::cout << "gRPC AMF : " << grpc_addr << "\n";
    std::cout << "REST AMF : " << rest_host << ":" << rest_port << "\n";
    std::cout << "Requests : " << n << " per transport\n\n";

    std::cout << "[1/2] Benchmarking gRPC...\n";
    auto grpc_stats = bench_grpc(grpc_addr, n);
    print_stats("gRPC (Protobuf/HTTP2)", grpc_stats);

    std::cout << "\n[2/2] Benchmarking REST...\n";
    auto rest_stats = bench_rest(rest_host, rest_port, n);
    print_stats("REST (JSON/HTTP)", rest_stats);

    std::cout << "\n══════════════════════════════════════════════════\n";
    std::cout << "  TRANSPORT COMPARISON SUMMARY\n";
    std::cout << "══════════════════════════════════════════════════\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  gRPC throughput : " << grpc_stats.throughput << " req/sec\n";
    std::cout << "  REST throughput : " << rest_stats.throughput << " req/sec\n";
    std::cout << "  gRPC P50 latency: " << grpc_stats.p50_us    << " µs\n";
    std::cout << "  REST P50 latency: " << rest_stats.p50_us    << " µs\n";
    std::cout << "  gRPC P99 latency: " << grpc_stats.p99_us    << " µs\n";
    std::cout << "  REST P99 latency: " << rest_stats.p99_us    << " µs\n";

    double tp_ratio  = grpc_stats.throughput / rest_stats.throughput;
    double lat_ratio = rest_stats.p50_us     / grpc_stats.p50_us;
    std::cout << "  gRPC throughput advantage : " << tp_ratio  << "x\n";
    std::cout << "  gRPC latency advantage    : " << lat_ratio << "x faster at P50\n";
    std::cout << "══════════════════════════════════════════════════\n\n";

    return 0;
}
