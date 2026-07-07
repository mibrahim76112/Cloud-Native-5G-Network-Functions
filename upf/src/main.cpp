#include <iostream>
#include <csignal>
#include <cstdlib>
#include <sstream>

#include <grpcpp/grpcpp.h>
#include "upf_server.h"
#include "../../metrics/prometheus_exporter.h"

static std::unique_ptr<grpc::Server> g_server;
static UpfServiceImpl*               g_service = nullptr;

void handle_signal(int /*sig*/) {
    std::cout << "\n[UPF] Shutting down...\n";
    if (g_server) g_server->Shutdown();
}

int main() {
    const char* port_env    = std::getenv("UPF_PORT");
    const char* threads_env = std::getenv("UPF_THREADS");
    const char* metrics_env = std::getenv("UPF_METRICS_PORT");

    std::string listen_addr  = std::string("0.0.0.0:") + (port_env ? port_env : "50052");
    size_t      num_threads  = threads_env ? std::stoul(threads_env) : 0;
    int         metrics_port = metrics_env ? std::stoi(metrics_env) : 9092;

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G UPF  (User Plane Function)     ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[UPF] Listening on : " << listen_addr << "\n";
    std::cout << "[UPF] Thread pool  : "
              << (num_threads > 0 ? std::to_string(num_threads) + " workers"
                                  : "disabled (single-threaded)") << "\n";
    std::cout << "[UPF] Metrics port : " << metrics_port << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    UpfServiceImpl service(num_threads);
    g_service = &service;

    // Start Prometheus metrics exporter
    PrometheusExporter exporter(metrics_port, [&]() {
        std::ostringstream oss;
        oss << prom_counter(
            "upf_packets_classified_total",
            "Total packets classified by UPF",
            service.packets_classified());
        oss << prom_counter(
            "upf_packets_forwarded_total",
            "Total packets forwarded by UPF",
            service.packets_forwarded());
        oss << prom_counter(
            "upf_packets_dropped_total",
            "Total packets dropped by UPF",
            service.packets_dropped());
        oss << prom_gauge(
            "upf_active_sessions",
            "Number of active PDU sessions",
            (double)service.active_sessions());
        oss << prom_gauge(
            "upf_thread_pool_size",
            "Number of worker threads in classification pool",
            (double)service.thread_count());
        return oss.str();
    });
    exporter.start();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::NUM_CQS, 4);
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 2);
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 8);

    g_server = builder.BuildAndStart();

    if (!g_server) {
        std::cerr << "[UPF] Failed to start on " << listen_addr << "\n";
        return 1;
    }

    std::cout << "[UPF] Ready.\n\n";
    g_server->Wait();

    std::cout << "[UPF] ── Final Metrics ──────────────────\n";
    std::cout << "[UPF] Thread pool size  : " << service.thread_count()      << "\n";
    std::cout << "[UPF] Active sessions   : " << service.active_sessions()   << "\n";
    std::cout << "[UPF] Packets classified: " << service.packets_classified() << "\n";
    std::cout << "[UPF] Packets forwarded : " << service.packets_forwarded()  << "\n";
    std::cout << "[UPF] Packets dropped   : " << service.packets_dropped()    << "\n";

    return 0;
}
