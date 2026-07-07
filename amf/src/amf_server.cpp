#include "amf_server.h"

#include <iostream>
#include <sstream>
#include <chrono>

#include <grpcpp/grpcpp.h>

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

AmfServiceImpl::AmfServiceImpl(const std::string& upf_address) {
    auto channel = grpc::CreateChannel(upf_address, grpc::InsecureChannelCredentials());
    upf_stub_ = n4::N4Service::NewStub(channel);
    std::cout << "[AMF] Connected to UPF at " << upf_address << "\n";
}

grpc::Status AmfServiceImpl::Register(
    grpc::ServerContext* /*ctx*/,
    const n1::RegistrationRequest* req,
    n1::RegistrationResponse* resp)
{
    std::cout << "[AMF] Registration request from SUPI: " << req->supi()
              << "  type: " << req->registration_type() << "\n";

    if (req->supi().empty()) {
        resp->set_accepted(false);
        resp->set_cause("missing_supi");
        resp->set_timestamp_us(now_us());
        return grpc::Status::OK;
    }

    std::lock_guard<std::mutex> lock(ctx_mutex_);

    if (ue_contexts_.count(req->supi()) &&
        req->registration_type() == "initial") {
        resp->set_accepted(false);
        resp->set_cause("already_registered");
        resp->set_timestamp_us(now_us());
        return grpc::Status::OK;
    }

    UEContext ue;
    ue.supi           = req->supi();
    ue.amf_ue_ngap_id = generate_ngap_id();
    ue.nssai          = req->requested_nssai();
    ue.registered     = true;

    std::string session_id;
    if (!establish_upf_session(ue, session_id)) {
        resp->set_accepted(false);
        resp->set_cause("upf_session_failed");
        resp->set_timestamp_us(now_us());
        return grpc::Status::OK;
    }
    ue.upf_session_id = session_id;

    ue_contexts_[ue.supi] = ue;
    reg_count_++;

    resp->set_accepted(true);
    resp->set_amf_ue_ngap_id(ue.amf_ue_ngap_id);
    resp->set_timestamp_us(now_us());

    std::cout << "[AMF] Registered UE " << ue.supi
              << "  NGAP-ID: " << ue.amf_ue_ngap_id
              << "  UPF session: " << session_id << "\n";

    return grpc::Status::OK;
}

grpc::Status AmfServiceImpl::Deregister(
    grpc::ServerContext* /*ctx*/,
    const n1::DeregistrationRequest* req,
    n1::DeregistrationResponse* resp)
{
    std::cout << "[AMF] Deregistration request from SUPI: " << req->supi() << "\n";

    std::lock_guard<std::mutex> lock(ctx_mutex_);

    auto it = ue_contexts_.find(req->supi());
    if (it == ue_contexts_.end()) {
        resp->set_success(false);
        resp->set_cause("ue_not_found");
        return grpc::Status::OK;
    }

    n4::SessionReleaseRequest rel_req;
    rel_req.set_session_id(it->second.upf_session_id);
    rel_req.set_supi(req->supi());

    n4::SessionReleaseResponse rel_resp;
    grpc::ClientContext upf_ctx;
    upf_stub_->ReleaseSession(&upf_ctx, rel_req, &rel_resp);

    ue_contexts_.erase(it);
    dereg_count_++;

    resp->set_success(true);
    std::cout << "[AMF] Deregistered UE " << req->supi() << "\n";

    return grpc::Status::OK;
}

std::string AmfServiceImpl::generate_ngap_id() {
    uint64_t id = ngap_id_counter_++;
    std::ostringstream oss;
    oss << "ngap-" << id;
    return oss.str();
}

bool AmfServiceImpl::establish_upf_session(
    const UEContext& ue_ctx, std::string& session_id_out)
{
    n4::SessionEstablishRequest req;
    req.set_supi(ue_ctx.supi);
    req.set_dnn("internet");
    req.set_ue_ip("10.0.0." + std::to_string(ngap_id_counter_.load() % 254 + 1));
    req.set_qos_profile(9);

    n4::SessionEstablishResponse resp;
    grpc::ClientContext ctx;

    auto status = upf_stub_->EstablishSession(&ctx, req, &resp);

    if (!status.ok()) {
        std::cerr << "[AMF] UPF gRPC error: " << status.error_message() << "\n";
        return false;
    }

    if (!resp.success()) {
        std::cerr << "[AMF] UPF rejected session: " << resp.cause() << "\n";
        return false;
    }

    session_id_out = resp.session_id();
    return true;
}
