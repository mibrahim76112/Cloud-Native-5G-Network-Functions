#include <iostream>
#include <csignal>
#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include "upf_server.h"

static std::unique_ptr<grpc::Server> g_server;
static UpfServiceImpl*               g_service = nullptr;

void handle_signal(int /*sig*/) {
    std::cout << "\n[UPF] Shutting down...\n";
    if (g_server) g_server->Shutdown();
}

int main() {
    const char* port_env    = std::getenv("UPF_PORT");
    const char* threads_env = std::getenv("UPF_THREADS");

    std::string listen_addr  = std::string("0.0.0.0:") + (port_env ? port_env : "50052");
    size_t      num_threads  = threads_env ? std::stoul(threads_env) : 0;

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G UPF  (User Plane Function)     ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[UPF] Listening on : " << listen_addr << "\n";
    std::cout << "[UPF] Thread pool  : "
              << (num_threads > 0 ? std::to_string(num_threads) + " workers"
                                  : "disabled (single-threaded)") << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    UpfServiceImpl service(num_threads);
    g_service = &service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);

    // Use more gRPC completion queue threads to handle concurrent requests
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
    std::cout << "[UPF] Thread pool size  : " << service.thread_count()       << "\n";
    std::cout << "[UPF] Active sessions   : " << service.active_sessions()    << "\n";
    std::cout << "[UPF] Packets classified: " << service.packets_classified()  << "\n";
    std::cout << "[UPF] Packets forwarded : " << service.packets_forwarded()   << "\n";
    std::cout << "[UPF] Packets dropped   : " << service.packets_dropped()     << "\n";

    return 0;
}
