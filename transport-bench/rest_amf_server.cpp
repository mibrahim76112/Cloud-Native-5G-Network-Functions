// REST AMF Server — HTTP/2 alternative to gRPC for SBA interfaces
// In 3GPP 5G Service Based Architecture, NFs communicate via HTTP/2 REST.
// This implements the same AMF registration logic as amf_server.cpp
// but over REST so we can benchmark gRPC vs REST transport directly.

#include "httplib.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>

// Simple JSON helpers (no external dependency)
static std::string make_json_response(bool accepted, const std::string& ngap_id,
                                       const std::string& cause, int64_t ts) {
    std::ostringstream oss;
    oss << "{"
        << "\"accepted\":" << (accepted ? "true" : "false") << ","
        << "\"amf_ue_ngap_id\":\"" << ngap_id << "\","
        << "\"cause\":\"" << cause << "\","
        << "\"timestamp_us\":" << ts
        << "}";
    return oss.str();
}

static std::string extract_field(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = json.find("\"", pos);
    return json.substr(pos, end - pos);
}

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

struct UEContext {
    std::string supi;
    std::string ngap_id;
    bool        registered = false;
};

int main() {
    const char* port_env = std::getenv("REST_AMF_PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    std::unordered_map<std::string, UEContext> ue_contexts;
    std::mutex                                  ctx_mutex;
    std::atomic<uint64_t>                       ngap_counter{1000};
    std::atomic<uint64_t>                       reg_count{0};

    httplib::Server svr;

    // ── POST /namf-comm/v1/ue-contexts/{supi}/register ──────────────────
    // Mirrors 3GPP TS 29.518 AMF registration interface
    svr.Post("/namf-comm/v1/ue-contexts/:supi/register",
        [&](const httplib::Request& req, httplib::Response& res) {
            auto supi = req.path_params.at("supi");
            auto body = req.body;

            auto reg_type = extract_field(body, "registration_type");

            std::lock_guard<std::mutex> lock(ctx_mutex);

            if (ue_contexts.count(supi) && reg_type == "initial") {
                res.set_content(
                    make_json_response(false, "", "already_registered", now_us()),
                    "application/json");
                return;
            }

            UEContext ue;
            ue.supi       = supi;
            ue.ngap_id    = "ngap-" + std::to_string(ngap_counter++);
            ue.registered = true;
            ue_contexts[supi] = ue;
            reg_count++;

            res.status = 201;
            res.set_content(
                make_json_response(true, ue.ngap_id, "", now_us()),
                "application/json");
        });

    // ── DELETE /namf-comm/v1/ue-contexts/{supi} ─────────────────────────
    svr.Delete("/namf-comm/v1/ue-contexts/:supi",
        [&](const httplib::Request& req, httplib::Response& res) {
            auto supi = req.path_params.at("supi");

            std::lock_guard<std::mutex> lock(ctx_mutex);
            ue_contexts.erase(supi);

            res.set_content("{\"success\":true}", "application/json");
        });

    // ── GET /metrics ─────────────────────────────────────────────────────
    svr.Get("/metrics", [&](const httplib::Request&, httplib::Response& res) {
        std::ostringstream oss;
        oss << "rest_amf_registrations_total " << reg_count.load() << "\n";
        oss << "rest_amf_active_ues " << ue_contexts.size() << "\n";
        res.set_content(oss.str(), "text/plain");
    });

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   5G AMF REST Server (SBA/HTTP)     ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "[REST-AMF] Listening on port " << port << "\n";
    std::cout << "[REST-AMF] Endpoint: POST /namf-comm/v1/ue-contexts/:supi/register\n\n";

    svr.listen("0.0.0.0", port);
    return 0;
}
