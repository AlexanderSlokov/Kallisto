#include <iostream>
#include "kallisto/kallisto.hpp"
#include "kallisto/logger.hpp"

int main() {
    // 1. Setup Logger
    kallisto::LogConfig config("kallisto.server.log");
    config.logFilePath = "logs/";
    config.name = "Kallisto";
    config.logLevel = "debug";
    
    try {
        kallisto::Logger::getInstance().setup(config);
        
        kallisto::info("===============");
        kallisto::info("   KALLISTO    ");
        kallisto::info("===============");
        
        kallisto::KallistoServer server;
        kallisto::info("Initializing subsystems... [Persistence Enabled]");
        
        // Test Data
        std::string path = "/prod/payment";
        std::string key = "db_pass";
        std::string secret = "SuperSecretPassword123";

        // 1. Try to RETRIEVE first (to prove persistence)
        kallisto::info("Checking for existing secret...");
        std::string existing = server.get_secret(path, key);
        
        if (!existing.empty()) {
            kallisto::info(">>> PERSISTENCE SUCCESS! Found secret from disk: " + existing);
        } else {
            kallisto::warn(">>> FIRST RUN OR NO DATA. Writing secret to disk...");
            if (server.put_secret(path, key, secret)) {
                kallisto::info("Secret stored. Restart server to verify persistence.");
            }
        }

    } catch (const std::exception& e) {
        
    } catch (const std::exception& e) {
        std::cerr << "Critical error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
