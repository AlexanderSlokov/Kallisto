#include "kallisto/server/grpc_handler.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include "kallisto/logger.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include "kallisto.grpc.pb.h"
#include <chrono>

namespace kallisto {
namespace server {

// ---------------------------------------------------------------------------
// SecretServiceImpl: The actual gRPC service implementation
// ---------------------------------------------------------------------------
class GrpcHandler::SecretServiceImpl final : public ::kallisto::SecretService::AsyncService {
    // AsyncService handles request routing via CompletionQueue
};

// ---------------------------------------------------------------------------
// CallData base: common interface for CQ tag dispatch
// ---------------------------------------------------------------------------
class GrpcHandler::CallData {
public:
    virtual ~CallData() = default;
    virtual void Proceed(bool ok) = 0;
};

// ---------------------------------------------------------------------------
// Typed CallData: each RPC type gets its own correctly-typed responder
// ---------------------------------------------------------------------------
template<typename Req, typename Resp>
class TypedCallData : public GrpcHandler::CallData {
public:
    enum class Status { CREATE, PROCESS, FINISH };
    
    using RequestFunc = std::function<void(
        grpc::ServerContext*, Req*, 
        grpc::ServerAsyncResponseWriter<Resp>*,
        grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void*)>;
    using HandleFunc = std::function<void(const Req&, Resp*, grpc::Status*)>;
    using SpawnFunc = std::function<void()>;
    
    TypedCallData(RequestFunc request_fn, HandleFunc handle_fn, SpawnFunc spawn_fn)
        : request_fn_(std::move(request_fn))
        , handle_fn_(std::move(handle_fn))
        , spawn_fn_(std::move(spawn_fn))
        , responder_(&ctx_)
        , status_(Status::CREATE) {
        Proceed(true);
    }
    
    void Proceed(bool ok) override {
        switch (status_) {
            case Status::CREATE:
                status_ = Status::PROCESS;
                request_fn_(&ctx_, &req_, &responder_, nullptr, nullptr, this);
                break;
                
            case Status::PROCESS: {
                // Spawn replacement to accept next request of same type
                spawn_fn_();
                
                if (!ok) {
                    status_ = Status::FINISH;
                    responder_.FinishWithError(grpc::Status::CANCELLED, this);
                    return;
                }
                
                grpc::Status grpc_status = grpc::Status::OK;
                handle_fn_(req_, &resp_, &grpc_status);
                
                status_ = Status::FINISH;
                responder_.Finish(resp_, grpc_status, this);
                break;
            }
            case Status::FINISH:
                delete this;
                break;
        }
    }
    
private:
    RequestFunc request_fn_;
    HandleFunc handle_fn_;
    SpawnFunc spawn_fn_;
    grpc::ServerContext ctx_;
    Req req_;
    Resp resp_;
    grpc::ServerAsyncResponseWriter<Resp> responder_;
    Status status_;
};

// ---------------------------------------------------------------------------
// GrpcHandler implementation
// ---------------------------------------------------------------------------

GrpcHandler::GrpcHandler(event::Dispatcher& dispatcher,
                         std::shared_ptr<ShardedCuckooTable> storage,
                         std::shared_ptr<RocksDBStorage> persistence)
    : dispatcher_(dispatcher)
    , storage_(std::move(storage))
    , persistence_(std::move(persistence))
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
    // Explicitly enable SO_REUSEPORT to allow multiple workers to bind the same port
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 1);
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
    
    // Envoy-style: Spawn dedicated thread to poll CQ
    // This thread blocks on cq_->Next() and wakes up the main Dispatcher
    // via eventfd (dispatcher_.post()) when events arrive.
    // Latency: < 50us (vs 1ms timer)
    polling_thread_ = std::thread([this]() {
        pollLoop();
    });
    
    running_ = true;
}

void GrpcHandler::shutdown() {
    if (!running_) return;
    running_ = false;
    
    if (server_) {
        server_->Shutdown();
    }
    
    if (cq_) {
        cq_->Shutdown();
    }
    
    // Join the polling thread
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
    
    // Drain remaining events is now handled by the polling thread loop exit
    // or we can do a final pass here if needed, but usually Next() returns false
    // providing a clean exit.
    
    info("[GRPC] Server shut down");
}

#include <vector>

// ... (existing code)

void GrpcHandler::pollLoop() {
    void* tag;
    bool ok;
    
    // Direct Execution Model:
    // Run gRPC logic directly on this thread. 
    // Zero context switch. Zero eventfd overhead.
    // Relies on ShardedCuckooTable's thread-safety.
    
    while (cq_->Next(&tag, &ok)) {
        auto* call = static_cast<CallData*>(tag);
        call->Proceed(ok);
    }
}



void GrpcHandler::spawnNewCallData() {
    auto* svc = service_.get();
    auto* cq = cq_.get();
    auto stor = storage_;
    
    // GET
    auto spawn_get = [this]() { spawnGet(); };
    spawnGet();
    
    // PUT
    auto spawn_put = [this]() { spawnPut(); };
    spawnPut();
    
    // DELETE
    auto spawn_del = [this]() { spawnDelete(); };
    spawnDelete();
    
    // LIST
    auto spawn_list = [this]() { spawnList(); };
    spawnList();
}

void GrpcHandler::spawnGet() {
    auto* svc = service_.get();
    auto* cq = cq_.get();
    auto stor = storage_;
    auto pers = persistence_;
    
    new TypedCallData<::kallisto::GetRequest, ::kallisto::GetResponse>(
        [svc, cq](grpc::ServerContext* ctx, ::kallisto::GetRequest* req,
                   grpc::ServerAsyncResponseWriter<::kallisto::GetResponse>* resp,
                   grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void* tag) {
            svc->RequestGet(ctx, req, resp, cq, cq, tag);
        },
        [stor, pers](const ::kallisto::GetRequest& req, ::kallisto::GetResponse* resp, 
               grpc::Status* status) {
            // Step 1: Try CuckooTable (hot cache)
            auto result = stor->lookup(req.path());
            
            // Step 2: Cache miss → fallback to RocksDB
            if (!result.has_value() && pers) {
                auto disk_result = pers->get(req.path());
                if (disk_result.has_value()) {
                    // Populate back into CuckooTable (thread-safe)
                    stor->insert(req.path(), disk_result.value());
                    result = std::move(disk_result);
                }
            }
            
            if (result.has_value()) {
                auto& entry = result.value();
                resp->set_value(entry.value);
                resp->set_created_at(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        entry.created_at.time_since_epoch()).count());
                resp->set_expires_at(0);
            } else {
                *status = grpc::Status(grpc::StatusCode::NOT_FOUND, 
                                       "Secret not found: " + req.path());
            }
        },
        [this]() { spawnGet(); }
    );
}

void GrpcHandler::spawnPut() {
    auto* svc = service_.get();
    auto* cq = cq_.get();
    auto stor = storage_;
    auto pers = persistence_;
    
    new TypedCallData<::kallisto::PutRequest, ::kallisto::PutResponse>(
        [svc, cq](grpc::ServerContext* ctx, ::kallisto::PutRequest* req,
                   grpc::ServerAsyncResponseWriter<::kallisto::PutResponse>* resp,
                   grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void* tag) {
            svc->RequestPut(ctx, req, resp, cq, cq, tag);
        },
        [stor, pers](const ::kallisto::PutRequest& req, ::kallisto::PutResponse* resp, 
               grpc::Status* status) {
            SecretEntry entry;
            entry.key = req.path();
            entry.value = req.value();
            entry.created_at = std::chrono::system_clock::now();
            
            // Write-Ahead: RocksDB FIRST, then CuckooTable
            if (pers) {
                bool persisted = pers->put(req.path(), entry);
                if (!persisted) {
                    resp->set_success(false);
                    resp->set_error("Failed to persist to disk");
                    return;
                }
            }
            
            bool ok = stor->insert(req.path(), entry);
            resp->set_success(ok);
            if (!ok) {
                resp->set_error("Failed to insert secret (table may be full)");
            }
        },
        [this]() { spawnPut(); }
    );
}

void GrpcHandler::spawnDelete() {
    auto* svc = service_.get();
    auto* cq = cq_.get();
    auto stor = storage_;
    auto pers = persistence_;
    
    new TypedCallData<::kallisto::DeleteRequest, ::kallisto::DeleteResponse>(
        [svc, cq](grpc::ServerContext* ctx, ::kallisto::DeleteRequest* req,
                   grpc::ServerAsyncResponseWriter<::kallisto::DeleteResponse>* resp,
                   grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void* tag) {
            svc->RequestDelete(ctx, req, resp, cq, cq, tag);
        },
        [stor, pers](const ::kallisto::DeleteRequest& req, ::kallisto::DeleteResponse* resp, 
               grpc::Status* status) {
            // RocksDB FIRST: ensure deletion is persisted
            if (pers) {
                bool persisted = pers->del(req.path());
                if (!persisted) {
                    resp->set_success(false);
                    return;
                }
            }
            
            stor->remove(req.path());
            resp->set_success(true);
        },
        [this]() { spawnDelete(); }
    );
}

void GrpcHandler::spawnList() {
    auto* svc = service_.get();
    auto* cq = cq_.get();
    auto stor = storage_;
    
    new TypedCallData<::kallisto::ListRequest, ::kallisto::ListResponse>(
        [svc, cq](grpc::ServerContext* ctx, ::kallisto::ListRequest* req,
                   grpc::ServerAsyncResponseWriter<::kallisto::ListResponse>* resp,
                   grpc::CompletionQueue*, grpc::ServerCompletionQueue*, void* tag) {
            svc->RequestList(ctx, req, resp, cq, cq, tag);
        },
        [stor](const ::kallisto::ListRequest& req, ::kallisto::ListResponse* resp, 
               grpc::Status* status) {
            auto entries = stor->get_all_entries();
            for (const auto& entry : entries) {
                if (req.prefix().empty() || 
                    entry.key.find(req.prefix()) == 0) {
                    resp->add_paths(entry.key);
                    if (req.limit() > 0 && 
                        resp->paths_size() >= req.limit()) {
                        break;
                    }
                }
            }
        },
        [this]() { spawnList(); }
    );
}

} // namespace server
} // namespace kallisto
