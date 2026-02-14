#include "kallisto/server/grpc_handler.hpp"
#include "kallisto/logger.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include "kallisto.grpc.pb.h"

namespace kallisto {
namespace server {

// ---------------------------------------------------------------------------
// SecretServiceImpl: The actual gRPC service implementation
// ---------------------------------------------------------------------------
class GrpcHandler::SecretServiceImpl final : public ::kallisto::SecretService::AsyncService {
    // AsyncService handles request routing via CompletionQueue
};

// ---------------------------------------------------------------------------
// CallData: Manages the lifecycle of a single async RPC
// ---------------------------------------------------------------------------
class GrpcHandler::CallData {
public:
    enum class Type { GET, PUT, DELETE, LIST };
    enum class Status { CREATE, PROCESS, FINISH };
    
    CallData(Type type,
             SecretServiceImpl* service,
             grpc::ServerCompletionQueue* cq,
             std::shared_ptr<ShardedCuckooTable> storage)
        : type_(type)
        , service_(service)
        , cq_(cq)
        , storage_(std::move(storage))
        , responder_(&ctx_)
        , status_(Status::CREATE) {
        Proceed(true);
    }
    
    void Proceed(bool ok) {
        switch (status_) {
            case Status::CREATE:
                status_ = Status::PROCESS;
                RequestRpc();
                break;
                
            case Status::PROCESS:
                // Spawn new CallData to handle next request of same type
                new CallData(type_, service_, cq_, storage_);
                
                if (!ok) {
                    status_ = Status::FINISH;
                    responder_.Finish(grpc::Status::CANCELLED, this);
                    return;
                }
                
                HandleRequest();
                break;
                
            case Status::FINISH:
                delete this;
                break;
        }
    }
    
private:
    void RequestRpc() {
        switch (type_) {
            case Type::GET:
                service_->RequestGet(&ctx_, &get_req_, &responder_, cq_, cq_, this);
                break;
            case Type::PUT:
                service_->RequestPut(&ctx_, &put_req_, &responder_, cq_, cq_, this);
                break;
            case Type::DELETE:
                service_->RequestDelete(&ctx_, &del_req_, &responder_, cq_, cq_, this);
                break;
            case Type::LIST:
                service_->RequestList(&ctx_, &list_req_, &responder_, cq_, cq_, this);
                break;
        }
    }
    
    void HandleRequest() {
        grpc::Status grpc_status = grpc::Status::OK;
        
        switch (type_) {
            case Type::GET: {
                auto result = storage_->lookup(get_req_.path());
                if (result.has_value()) {
                    auto& entry = result.value();
                    get_resp_.set_value(entry.value);
                    get_resp_.set_created_at(entry.timestamp);
                    get_resp_.set_expires_at(0);
                } else {
                    grpc_status = grpc::Status(grpc::StatusCode::NOT_FOUND, 
                                               "Secret not found: " + get_req_.path());
                }
                status_ = Status::FINISH;
                responder_.Finish(get_resp_, grpc_status, this);
                break;
            }
            case Type::PUT: {
                SecretEntry entry;
                entry.key = put_req_.path();
                entry.value = put_req_.value();
                entry.timestamp = std::time(nullptr);
                
                bool ok = storage_->insert(put_req_.path(), entry);
                put_resp_.set_success(ok);
                if (!ok) {
                    put_resp_.set_error("Failed to insert secret (table may be full)");
                }
                status_ = Status::FINISH;
                responder_.Finish(put_resp_, grpc::Status::OK, this);
                break;
            }
            case Type::DELETE: {
                bool ok = storage_->remove(del_req_.path());
                del_resp_.set_success(ok);
                status_ = Status::FINISH;
                responder_.Finish(del_resp_, grpc::Status::OK, this);
                break;
            }
            case Type::LIST: {
                auto entries = storage_->get_all_entries();
                for (const auto& entry : entries) {
                    if (list_req_.prefix().empty() || 
                        entry.key.find(list_req_.prefix()) == 0) {
                        list_resp_.add_paths(entry.key);
                        if (list_req_.limit() > 0 && 
                            list_resp_.paths_size() >= list_req_.limit()) {
                            break;
                        }
                    }
                }
                status_ = Status::FINISH;
                responder_.Finish(list_resp_, grpc::Status::OK, this);
                break;
            }
        }
    }
    
    Type type_;
    SecretServiceImpl* service_;
    grpc::ServerCompletionQueue* cq_;
    std::shared_ptr<ShardedCuckooTable> storage_;
    grpc::ServerContext ctx_;
    
    // Request/Response objects (only one pair is used depending on type_)
    ::kallisto::GetRequest get_req_;
    ::kallisto::GetResponse get_resp_;
    ::kallisto::PutRequest put_req_;
    ::kallisto::PutResponse put_resp_;
    ::kallisto::DeleteRequest del_req_;
    ::kallisto::DeleteResponse del_resp_;
    ::kallisto::ListRequest list_req_;
    ::kallisto::ListResponse list_resp_;
    
    grpc::ServerAsyncResponseWriter<google::protobuf::Message> responder_;
    Status status_;
};

// ---------------------------------------------------------------------------
// GrpcHandler implementation
// ---------------------------------------------------------------------------

GrpcHandler::GrpcHandler(event::Dispatcher& dispatcher,
                         std::shared_ptr<ShardedCuckooTable> storage)
    : dispatcher_(dispatcher)
    , storage_(std::move(storage))
    , service_(std::make_unique<SecretServiceImpl>()) {
}

GrpcHandler::~GrpcHandler() {
    shutdown();
}

void GrpcHandler::initialize(uint16_t port) {
    if (running_) return;
    
    // Enable gRPC reflection for grpcurl debugging
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:" + std::to_string(port),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    
    if (!server_) {
        error("[GRPC] Failed to start gRPC server on port " + std::to_string(port));
        return;
    }
    
    info("[GRPC] Server listening on port " + std::to_string(port));
    
    // Spawn initial CallData for each RPC type
    spawnNewCallData();
    
    // Timer-based CQ polling (1ms interval)
    // Accepted tradeoff: ~1ms latency overhead for much simpler code.
    // Upgrade to eventfd bridge when benchmark shows jitter.
    poll_timer_ = dispatcher_.createTimer([this]() {
        pollCompletionQueue();
        if (running_) {
            poll_timer_->enableTimer(1);  // Re-arm: poll every 1ms
        }
    });
    poll_timer_->enableTimer(1);
    
    running_ = true;
}

void GrpcHandler::shutdown() {
    if (!running_) return;
    running_ = false;
    
    if (poll_timer_) {
        poll_timer_->disableTimer();
    }
    
    if (server_) {
        server_->Shutdown();
    }
    
    if (cq_) {
        cq_->Shutdown();
        
        // Drain remaining events
        void* tag;
        bool ok;
        while (cq_->Next(&tag, &ok)) {
            auto* call = static_cast<CallData*>(tag);
            delete call;
        }
    }
    
    info("[GRPC] Server shut down");
}

void GrpcHandler::pollCompletionQueue() {
    void* tag;
    bool ok;
    
    // Non-blocking drain: process all available events
    while (cq_->AsyncNext(&tag, &ok, 
           gpr_time_0(GPR_CLOCK_REALTIME)) == grpc::CompletionQueue::GOT_EVENT) {
        auto* call = static_cast<CallData*>(tag);
        call->Proceed(ok);
    }
}

void GrpcHandler::spawnNewCallData() {
    // Create one CallData per RPC type to start accepting requests
    new CallData(CallData::Type::GET, service_.get(), cq_.get(), storage_);
    new CallData(CallData::Type::PUT, service_.get(), cq_.get(), storage_);
    new CallData(CallData::Type::DELETE, service_.get(), cq_.get(), storage_);
    new CallData(CallData::Type::LIST, service_.get(), cq_.get(), storage_);
}

} // namespace server
} // namespace kallisto
