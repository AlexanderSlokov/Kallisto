#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <grpcpp/grpcpp.h>
#include "kallisto.grpc.pb.h"
#include <iomanip>

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using kallisto::SecretService;
using kallisto::GetRequest;
using kallisto::GetResponse;
using kallisto::PutRequest;
using kallisto::PutResponse;

class BenchClient {
public:
    BenchClient(std::shared_ptr<Channel> channel)
        : stub_(SecretService::NewStub(channel)) {}

    void Run(int concurrency, int duration_seconds, const std::string& op) {
        std::cout << "Starting gRPC Benchmark: " << op 
                  << " | Concurrency: " << concurrency 
                  << " | Duration: " << duration_seconds << "s" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();
        auto end_time = start_time + std::chrono::seconds(duration_seconds);

        // Bootstrap with 'concurrency' requests
        for (int i = 0; i < concurrency; ++i) {
            if (op == "PUT") {
                SendPut(i);
            } else {
                SendGet(i);
            }
        }

        // Event loop
        void* got_tag;
        bool ok = false;
        while (cq_.Next(&got_tag, &ok)) {
            AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
            
            // Validate result
            if (call->status.ok()) {
                request_count_++;
            } else {
                error_count_++;
                // DEBUG: Print error if any (limit to first 5 errors)
                if (error_count_ <= 5) {
                    std::cerr << "Error: " << call->status.error_code() << ": " << call->status.error_message() << std::endl;
                }
            }
            
            // DEBUG: Print progress every 5000 requests
            if (request_count_ % 5000 == 0) {
                 std::cout << "." << std::flush;
            }

            // Clean up previous call
            delete call;
            active_requests_--;

            // Check if we should stop
            auto now = std::chrono::high_resolution_clock::now();
            if (now >= end_time) {
                if (active_requests_ == 0) {
                    cq_.Shutdown();
                    break; 
                }
                continue; // Drain queue
            }

            // Schedule next request
            if (op == "PUT") {
                SendPut(request_count_);
            } else {
                SendGet(request_count_);
            }
        }
        
        auto total_time = std::chrono::high_resolution_clock::now() - start_time;
        double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(total_time).count();
        
        std::cout << "\n--- Results ---" << std::endl;
        std::cout << "Requests: " << request_count_ << std::endl;
        std::cout << "Errors:   " << error_count_ << std::endl;
        std::cout << "Duration: " << seconds << "s" << std::endl;
        std::cout << "RPS:      " << (request_count_ / seconds) << std::endl;
    }

private:
    struct AsyncClientCall {
        GetRequest get_req;
        GetResponse get_res;
        PutRequest put_req;
        PutResponse put_res;
        ClientContext context;
        Status status;
        std::unique_ptr<ClientAsyncResponseReader<GetResponse>> get_responder;
        std::unique_ptr<ClientAsyncResponseReader<PutResponse>> put_responder;
    };

    void SendPut(int id) {
        AsyncClientCall* call = new AsyncClientCall;
        call->put_req.set_path("bench/k" + std::to_string(id % 1000));
        call->put_req.set_value("value_" + std::to_string(id));
        call->put_responder = stub_->PrepareAsyncPut(&call->context, call->put_req, &cq_);
        call->put_responder->StartCall();
        call->put_responder->Finish(&call->put_res, &call->status, (void*)call);
        active_requests_++;
    }

    void SendGet(int id) {
        AsyncClientCall* call = new AsyncClientCall;
        call->get_req.set_path("bench/k" + std::to_string(id % 1000));
        call->get_responder = stub_->PrepareAsyncGet(&call->context, call->get_req, &cq_);
        call->get_responder->StartCall();
        call->get_responder->Finish(&call->get_res, &call->status, (void*)call);
        active_requests_++;
    }

    std::unique_ptr<SecretService::Stub> stub_;
    CompletionQueue cq_;
    std::atomic<long> request_count_{0};
    std::atomic<long> error_count_{0};
    std::atomic<int> active_requests_{0};
};

int main(int argc, char** argv) {
    std::string target_str = "127.0.0.1:8201";
    int concurrency = 50;
    int duration = 10;
    std::string op = "GET";

    if (argc > 1) op = argv[1]; 
    if (argc > 2) concurrency = std::atoi(argv[2]);

    BenchClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    client.Run(concurrency, duration, op);

    return 0;
}
