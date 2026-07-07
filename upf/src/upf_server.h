#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>

#include <grpcpp/grpcpp.h>
#include "n4.grpc.pb.h"
#include "thread_pool.h"

struct PduSession {
    std::string session_id;
    std::string supi;
    std::string ue_ip;
    std::string dnn;
    uint32_t    qos_profile;
    bool        active = true;
};

class UpfServiceImpl final : public n4::N4Service::Service {
public:
    // num_threads: size of thread pool for packet classification
    // 0 = single-threaded (baseline), >0 = multi-threaded
    explicit UpfServiceImpl(size_t num_threads = 0);
    grpc::Status EstablishSession(
        grpc::ServerContext* ctx,
        const n4::SessionEstablishRequest* req,
        n4::SessionEstablishResponse* resp) override;

    grpc::Status ReleaseSession(
        grpc::ServerContext* ctx,
        const n4::SessionReleaseRequest* req,
        n4::SessionReleaseResponse* resp) override;

    grpc::Status ClassifyPacket(
        grpc::ServerContext* ctx,
        const n4::PacketClassifyRequest* req,
        n4::PacketClassifyResponse* resp) override;

    // Metrics
    uint64_t active_sessions()    const;
    uint64_t packets_classified() const { return pkt_count_.load(); }
    uint64_t packets_forwarded()  const { return fwd_count_.load(); }
    uint64_t packets_dropped()    const { return drop_count_.load(); }
    size_t   thread_count()       const {
        return pool_ ? pool_->num_threads() : 0;
    }

private:
    std::string generate_session_id();
    std::string classify(
        const n4::PacketClassifyRequest* req,
        uint32_t& qos_out,
        std::string& next_hop_out);

    void do_classify(
        const n4::PacketClassifyRequest req,
        n4::PacketClassifyResponse* resp,
        grpc::ServerContext* ctx);

    std::unordered_map<std::string, PduSession> sessions_;
    std::unordered_map<std::string, std::string> ue_to_session_;
    std::mutex sessions_mutex_;

    // Thread pool — nullptr means single-threaded mode
    std::unique_ptr<ThreadPool> pool_;

    std::atomic<uint64_t> session_id_counter_{1};
    std::atomic<uint64_t> pkt_count_{0};
    std::atomic<uint64_t> fwd_count_{0};
    std::atomic<uint64_t> drop_count_{0};
};
