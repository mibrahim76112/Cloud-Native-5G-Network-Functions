#include <iostream>
#include <csignal>

#include <grpcpp/grpcpp.h>
#include "upf_server.h"

static std::unique_ptr<grpc::Server> g_server;
static UpfServiceImpl*               g_service = nullptr;

void handle_signal(int /*sig*/) {
    std::cout << "\n[UPF] Shutting down...\n";
    if (g_server) g_server->Shutdown();
}

int main() {
    const char* port_env = std::getenv("UPF_PORT");
    std::string listen_addr = std::string("0.0.0.0:") + (port_env ? port_env : "50052");

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G UPF  (User Plane Function)     ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[UPF] Listening on: " << listen_addr << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    UpfServiceImpl service;
    g_service = &service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);

    g_server = builder.BuildAndStart();

    if (!g_server) {
        std::cerr << "[UPF] Failed to start on " << listen_addr << "\n";
        return 1;
    }

    std::cout << "[UPF] Ready.\n\n";
    g_server->Wait();

    std::cout << "[UPF] ── Final Metrics ──────────────────\n";
    std::cout << "[UPF] Active sessions   : " << service.active_sessions()    << "\n";
    std::cout << "[UPF] Packets classified: " << service.packets_classified()  << "\n";
    std::cout << "[UPF] Packets forwarded : " << service.packets_forwarded()   << "\n";
    std::cout << "[UPF] Packets dropped   : " << service.packets_dropped()     << "\n";

    return 0;
}
