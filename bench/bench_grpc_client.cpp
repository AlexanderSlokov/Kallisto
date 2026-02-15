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
        : channel_(channel) {}

    void Run(int concurrency, int duration_seconds, const std::string& op, int num_threads) {
        std::cout << "Starting Multi-threaded gRPC Benchmark: " << op 
                  << " | Concurrency: " << concurrency 
                  << " | Threads: " << num_threads
                  << " | Duration: " << duration_seconds << "s" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();
        auto end_time = start_time + std::chrono::seconds(duration_seconds);

        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([this, concurrency, end_time, op, t, num_threads]() {
                CompletionQueue cq;
                auto stub = SecretService::NewStub(channel_);
                
                int thread_concurrency = concurrency / num_threads;
                int active_requests = 0;
                
                // Bootstrap
                for (int i = 0; i < thread_concurrency; ++i) {
                    SendReq(stub.get(), &cq, op, (t * 1000000) + i, active_requests);
                }

                void* got_tag;
                bool ok = false;
                while (active_requests > 0 && cq.Next(&got_tag, &ok)) {
                    AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
                    if (call->status.ok()) request_count_++;
                    else error_count_++;

                    delete call;
                    active_requests--;

                    if (std::chrono::high_resolution_clock::now() < end_time) {
                        SendReq(stub.get(), &cq, op, (t * 1000000) + (request_count_.load()), active_requests);
                    } else {
                        if (active_requests == 0) break;
                    }
                }
            });
        }

        for (auto& t : threads) t.join();
        
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

    void SendReq(SecretService::Stub* stub, CompletionQueue* cq, const std::string& op, int id, int& active) {
        AsyncClientCall* call = new AsyncClientCall;
        if (op == "PUT") {
            call->put_req.set_path("bench/k" + std::to_string(id % 10000));
            call->put_req.set_value("value_" + std::to_string(id));
            call->put_responder = stub->PrepareAsyncPut(&call->context, call->put_req, cq);
            call->put_responder->StartCall();
            call->put_responder->Finish(&call->put_res, &call->status, (void*)call);
        } else {
            call->get_req.set_path("bench/k" + std::to_string(id % 10000));
            call->get_responder = stub->PrepareAsyncGet(&call->context, call->get_req, cq);
            call->get_responder->StartCall();
            call->get_responder->Finish(&call->get_res, &call->status, (void*)call);
        }
        active++;
    }

    std::shared_ptr<Channel> channel_;
    std::atomic<long> request_count_{0};
    std::atomic<long> error_count_{0};
};

int main(int argc, char** argv) {
    std::string target_str = "127.0.0.1:8201";
    int concurrency = 200;
    int duration = 10;
    std::string op = "GET";
    int threads = std::thread::hardware_concurrency();

    if (argc > 1) op = argv[1]; 
    if (argc > 2) concurrency = std::atoi(argv[2]);
    if (argc > 3) threads = std::atoi(argv[3]);

    BenchClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    client.Run(concurrency, duration, op, threads);

    return 0;
}
