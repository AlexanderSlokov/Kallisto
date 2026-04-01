#include <gtest/gtest.h>
#include "kallisto/siphash.hpp"
#include <string>

using namespace kallisto;

// -----------------------------------------------------------------------------
// SIPHASH TEST SUITE
// Problem Description: SipHash is used for core routing and anti-DDOS tracking.
// We must ensure consistent hashing across architectures, proper remainder handling,
// and zero-behavior for edge cases.
// Goals:
// - 100% Branch Coverage on remainder switches
// - Boundary tests (empty, huge sizes)
// - Fuzz-like edge cases (embedded nulls)
// -----------------------------------------------------------------------------

TEST(SipHashTest, EmptyStringReturnsConsistentHash) {
    SipHash hasher(0x0, 0x0);
    uint64_t hash1 = hasher.hash("");
    uint64_t hash2 = hasher.hash("");
    EXPECT_EQ(hash1, hash2);
}

TEST(SipHashTest, DifferentKeysProduceDifferentHashes) {
    SipHash hasher1(0x1, 0x0);
    SipHash hasher2(0x0, 0x1);
    EXPECT_NE(hasher1.hash("kallisto"), hasher2.hash("kallisto"));
}

TEST(SipHashTest, SameKeysProduceSameHashes) {
    SipHash hasher1(0xDEADBEEF, 0xCAFEBABE);
    SipHash hasher2(0xDEADBEEF, 0xCAFEBABE);
    EXPECT_EQ(hasher1.hash("kallisto"), hasher2.hash("kallisto"));
}

TEST(SipHashTest, RemainderSwitchCoverage) {
    SipHash hasher(0x12345678, 0x87654321);
    
    // Test all lengths from 0 to 16 to comprehensively cover the byte-packing switch statement
    for (size_t len = 0; len <= 16; ++len) {
        std::string payload(len, 'A');
        EXPECT_NE(hasher.hash(payload), 0) << "Length " << len << " failed.";
    }
}

TEST(SipHashTest, NullCharactersAreHandledProperly) {
    SipHash hasher(0x1, 0x1);
    
    // Explicitly test strings with embedded null terminators
    std::string str1("kallisto\0core", 13);
    std::string str2("kallisto\0test", 13);
    
    EXPECT_NE(hasher.hash(str1), hasher.hash(str2));
}

TEST(SipHashTest, StaticMethodEqualsInstanceMethod) {
    SipHash hasher(0xDEADBEEF, 0xCAFEBABE);
    EXPECT_EQ(hasher.hash("kallisto_core"), 
              SipHash::hash("kallisto_core", 0xDEADBEEF, 0xCAFEBABE));
}

TEST(SipHashTest, VeryLongStringsOutputConsistentHash) {
    SipHash hasher(0x11111111, 0x22222222);
    std::string input(1000000, 'X'); // 1MB string
    
    uint64_t hash1 = hasher.hash(input);
    uint64_t hash2 = hasher.hash(input);
    
    EXPECT_EQ(hash1, hash2);
}
