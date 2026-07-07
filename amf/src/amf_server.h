#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include "n1.grpc.pb.h"
#include "n4.grpc.pb.h"

struct UEContext {
    std::string supi;
    std::string amf_ue_ngap_id;
    std::string nssai;
    std::string upf_session_id;
    bool        registered = false;
};

class AmfServiceImpl final : public n1::N1Service::Service {
public:
    explicit AmfServiceImpl(const std::string& upf_address);

    grpc::Status Register(
        grpc::ServerContext* ctx,
        const n1::RegistrationRequest* req,
        n1::RegistrationResponse* resp) override;

    grpc::Status Deregister(
        grpc::ServerContext* ctx,
        const n1::DeregistrationRequest* req,
        n1::DeregistrationResponse* resp) override;

    uint64_t total_registrations()   const { return reg_count_.load(); }
    uint64_t total_deregistrations() const { return dereg_count_.load(); }

private:
    std::string generate_ngap_id();
    bool establish_upf_session(const UEContext& ue_ctx, std::string& session_id_out);

    std::unordered_map<std::string, UEContext> ue_contexts_;
    std::mutex                                  ctx_mutex_;
    std::unique_ptr<n4::N4Service::Stub>        upf_stub_;

    std::atomic<uint64_t> reg_count_{0};
    std::atomic<uint64_t> dereg_count_{0};
    std::atomic<uint64_t> ngap_id_counter_{1000};
};
