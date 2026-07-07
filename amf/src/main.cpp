#include <iostream>
#include <string>
#include <csignal>
#include <sstream>

#include <grpcpp/grpcpp.h>
#include "amf_server.h"
#include "../../metrics/prometheus_exporter.h"

static std::unique_ptr<grpc::Server> g_server;
static AmfServiceImpl*               g_service = nullptr;

void handle_signal(int /*sig*/) {
    std::cout << "\n[AMF] Shutting down...\n";
    if (g_server) g_server->Shutdown();
}

int main() {
    const char* port_env    = std::getenv("AMF_PORT");
    const char* upf_env     = std::getenv("UPF_ADDRESS");
    const char* metrics_env = std::getenv("AMF_METRICS_PORT");

    std::string listen_addr  = std::string("0.0.0.0:") + (port_env ? port_env : "50051");
    std::string upf_address  = upf_env     ? upf_env     : "localhost:50052";
    int         metrics_port = metrics_env ? std::stoi(metrics_env) : 9091;

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G AMF  (Access & Mobility Mgmt)  ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[AMF] Listening on  : " << listen_addr  << "\n";
    std::cout << "[AMF] UPF address   : " << upf_address  << "\n";
    std::cout << "[AMF] Metrics port  : " << metrics_port << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    AmfServiceImpl service(upf_address);
    g_service = &service;

    // Start Prometheus metrics exporter
    PrometheusExporter exporter(metrics_port, [&]() {
        std::ostringstream oss;
        oss << prom_counter(
            "amf_registrations_total",
            "Total number of successful UE registrations",
            service.total_registrations());
        oss << prom_counter(
            "amf_deregistrations_total",
            "Total number of UE deregistrations",
            service.total_deregistrations());
        oss << prom_gauge(
            "amf_active_ues",
            "Number of currently registered UEs",
            (double)(service.total_registrations() -
                     service.total_deregistrations()));
        return oss.str();
    });
    exporter.start();

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
