#include <iostream>
#include <string>
#include <csignal>

#include <grpcpp/grpcpp.h>
#include "amf_server.h"

static std::unique_ptr<grpc::Server> g_server;

void handle_signal(int /*sig*/) {
    std::cout << "\n[AMF] Shutting down...\n";
    if (g_server) g_server->Shutdown();
}

int main() {
    const char* port_env = std::getenv("AMF_PORT");
    const char* upf_env  = std::getenv("UPF_ADDRESS");

    std::string listen_addr = std::string("0.0.0.0:") + (port_env ? port_env : "50051");
    std::string upf_address = upf_env ? upf_env : "localhost:50052";

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G AMF  (Access & Mobility Mgmt)  ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[AMF] Listening on : " << listen_addr << "\n";
    std::cout << "[AMF] UPF address  : " << upf_address << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    AmfServiceImpl service(upf_address);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);

    g_server = builder.BuildAndStart();

    if (!g_server) {
        std::cerr << "[AMF] Failed to start server\n";
        return 1;
    }

    std::cout << "[AMF] Ready.\n\n";
    g_server->Wait();

    std::cout << "[AMF] Registrations : " << service.total_registrations()   << "\n";
    std::cout << "[AMF] Deregistrations: " << service.total_deregistrations() << "\n";

    return 0;
}
