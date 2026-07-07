#include "upf_server.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <future>

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// ── Constructor ───────────────────────────────────────────────────────────

UpfServiceImpl::UpfServiceImpl(size_t num_threads) {
    if (num_threads > 0) {
        pool_ = std::make_unique<ThreadPool>(num_threads);
        std::cout << "[UPF] Multi-threaded mode: " << num_threads << " workers\n";
    } else {
        std::cout << "[UPF] Single-threaded mode (baseline)\n";
    }
}

// ── EstablishSession ──────────────────────────────────────────────────────

grpc::Status UpfServiceImpl::EstablishSession(
    grpc::ServerContext* /*ctx*/,
    const n4::SessionEstablishRequest* req,
    n4::SessionEstablishResponse* resp)
{
    std::cout << "[UPF] Session establish for SUPI: " << req->supi()
              << "  UE-IP: " << req->ue_ip()
              << "  DNN: "   << req->dnn() << "\n";

    if (req->supi().empty() || req->ue_ip().empty()) {
        resp->set_success(false);
        resp->set_cause("invalid_request");
        resp->set_timestamp_us(now_us());
        return grpc::Status::OK;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    PduSession session;
    session.session_id  = generate_session_id();
    session.supi        = req->supi();
    session.ue_ip       = req->ue_ip();
    session.dnn         = req->dnn();
    session.qos_profile = req->qos_profile();
    session.active      = true;

    sessions_[session.session_id] = session;
    ue_to_session_[session.ue_ip] = session.session_id;

    resp->set_success(true);
    resp->set_session_id(session.session_id);
    resp->set_timestamp_us(now_us());

    std::cout << "[UPF] Session created: " << session.session_id << "\n";
    return grpc::Status::OK;
}

// ── ReleaseSession ────────────────────────────────────────────────────────

grpc::Status UpfServiceImpl::ReleaseSession(
    grpc::ServerContext* /*ctx*/,
    const n4::SessionReleaseRequest* req,
    n4::SessionReleaseResponse* resp)
{
    std::cout << "[UPF] Session release: " << req->session_id() << "\n";

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto it = sessions_.find(req->session_id());
    if (it == sessions_.end()) {
        resp->set_success(false);
        resp->set_cause("session_not_found");
        return grpc::Status::OK;
    }

    ue_to_session_.erase(it->second.ue_ip);
    sessions_.erase(it);

    resp->set_success(true);
    std::cout << "[UPF] Session released: " << req->session_id() << "\n";
    return grpc::Status::OK;
}

// ── ClassifyPacket ────────────────────────────────────────────────────────
// If thread pool is enabled, classification runs on a worker thread.
// The gRPC call blocks until the worker completes via a promise/future.

grpc::Status UpfServiceImpl::ClassifyPacket(
    grpc::ServerContext* ctx,
    const n4::PacketClassifyRequest* req,
    n4::PacketClassifyResponse* resp)
{
    pkt_count_++;

    if (pool_) {
        // Multi-threaded: dispatch to thread pool
        // Use promise/future to block until worker finishes
        auto promise = std::make_shared<std::promise<void>>();
        auto future  = promise->get_future();

        // Copy request (req pointer is only valid during this call)
        n4::PacketClassifyRequest req_copy = *req;

        pool_->enqueue([this, req_copy, resp, promise]() mutable {
            uint32_t    qos_class = 0;
            std::string next_hop;
            std::string action = classify(&req_copy, qos_class, next_hop);

            resp->set_action(action);
            resp->set_next_hop(next_hop);
            resp->set_qos_class(qos_class);
            resp->set_timestamp_us(now_us());

            if (action == "FORWARD") fwd_count_++;
            else                     drop_count_++;

            promise->set_value();
        });

        future.wait();
    } else {
        // Single-threaded: classify directly
        uint32_t    qos_class = 0;
        std::string next_hop;
        std::string action = classify(req, qos_class, next_hop);

        resp->set_action(action);
        resp->set_next_hop(next_hop);
        resp->set_qos_class(qos_class);
        resp->set_timestamp_us(now_us());

        if (action == "FORWARD") fwd_count_++;
        else                     drop_count_++;
    }

    return grpc::Status::OK;
}

// ── Private helpers ───────────────────────────────────────────────────────

std::string UpfServiceImpl::generate_session_id() {
    std::ostringstream oss;
    oss << "sess-" << session_id_counter_++;
    return oss.str();
}

std::string UpfServiceImpl::classify(
    const n4::PacketClassifyRequest* req,
    uint32_t& qos_out,
    std::string& next_hop_out)
{
    // Lock only for session lookup — released before processing
    std::string session_id;
    uint32_t    qos_profile = 0;
    bool        session_active = false;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = ue_to_session_.find(req->src_ip());
        if (sit == ue_to_session_.end()) {
            qos_out      = 0;
            next_hop_out = "";
            return "DROP";
        }
        const PduSession& session = sessions_.at(sit->second);
        qos_profile    = session.qos_profile;
        session_active = session.active;
    }

    if (!session_active) {
        qos_out      = 0;
        next_hop_out = "";
        return "DROP";
    }

    qos_out = qos_profile;

    // Block RFC-1918 private destinations
    if (req->dst_ip().substr(0, 3) == "10." ||
        req->dst_ip().substr(0, 8) == "192.168.") {
        return "DROP";
    }

    if (qos_out == 1) {
        if (req->protocol() == "UDP" &&
            req->dst_port() >= 16384 && req->dst_port() <= 32767) {
            next_hop_out = "172.16.0.1";
            return "FORWARD";
        }
        return "DROP";
    }

    if (qos_out == 4) {
        if (req->dst_port() == 443 || req->dst_port() == 1935 ||
            (req->dst_port() >= 16384 && req->dst_port() <= 32767)) {
            next_hop_out = "172.16.0.1";
            return "FORWARD";
        }
        return "DROP";
    }

    next_hop_out = "172.16.0.1";
    return "FORWARD";
}

uint64_t UpfServiceImpl::active_sessions() const {
    return sessions_.size();
}
