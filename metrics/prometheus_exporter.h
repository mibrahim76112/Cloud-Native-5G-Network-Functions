#pragma once

// ── Prometheus Metrics Exporter ───────────────────────────────────────────
// Exposes NF metrics in Prometheus text format via a lightweight HTTP server.
// Standard in production 5G cores — Kubernetes scrapes this endpoint
// for autoscaling decisions and alerting.
//
// Endpoint: GET /metrics
// Format:   Prometheus text exposition format (text/plain)

#include "../transport-bench/httplib.h"

#include <string>
#include <atomic>
#include <thread>
#include <sstream>
#include <functional>
#include <iostream>

class PrometheusExporter {
public:
    // metrics_fn: called on each scrape to get current metric values
    PrometheusExporter(int port, std::function<std::string()> metrics_fn)
        : port_(port), metrics_fn_(metrics_fn), running_(false) {}

    void start() {
        running_ = true;
        server_thread_ = std::thread([this]() {
            httplib::Server svr;

            svr.Get("/metrics", [this](const httplib::Request&,
                                        httplib::Response& res) {
                res.set_content(metrics_fn_(), "text/plain; version=0.0.4");
                scrape_count_++;
            });

            svr.Get("/health", [](const httplib::Request&,
                                   httplib::Response& res) {
                res.set_content("{\"status\":\"ok\"}", "application/json");
            });

            std::cout << "[Prometheus] Metrics endpoint: http://0.0.0.0:"
                      << port_ << "/metrics\n";
            svr.listen("0.0.0.0", port_);
        });
        server_thread_.detach();
    }

    uint64_t scrape_count() const { return scrape_count_.load(); }

private:
    int                          port_;
    std::function<std::string()> metrics_fn_;
    std::atomic<bool>            running_;
    std::atomic<uint64_t>        scrape_count_{0};
    std::thread                  server_thread_;
};

// ── Metric helpers ────────────────────────────────────────────────────────
// Builds a Prometheus-formatted metric line with HELP and TYPE annotations.

inline std::string prom_counter(const std::string& name,
                                 const std::string& help,
                                 uint64_t value) {
    std::ostringstream oss;
    oss << "# HELP " << name << " " << help << "\n";
    oss << "# TYPE " << name << " counter\n";
    oss << name << " " << value << "\n";
    return oss.str();
}

inline std::string prom_gauge(const std::string& name,
                               const std::string& help,
                               double value) {
    std::ostringstream oss;
    oss << "# HELP " << name << " " << help << "\n";
    oss << "# TYPE " << name << " gauge\n";
    oss << name << " " << value << "\n";
    return oss.str();
}
