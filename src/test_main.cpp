#include <gtest/gtest.h>

#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// =============================================================================
// KALLISTO CLI TEST SUITE
//
// Problem Description:
//   The Kallisto Admin CLI is a lightweight Unix Domain Socket client.
//   We test its pure logic (argument parsing, command building, RAII guard)
//   without requiring a live server connection.
//
// Coverage:
//   - Argument parsing: single arg, multi arg, help flags
//   - RAII SocketGuard: auto-close, invalid fd safety
//   - Error paths: non-existent socket, recv on bad fd
// =============================================================================

// Include the CLI source in test mode (disables main())
#define KALLISTO_CLI_TEST_MODE
#include "cli.cpp"

// ---------------------------------------------------------------------------
// 1. Command Building from CLI Arguments
// ---------------------------------------------------------------------------

TEST(KallistoCliTest, BuildCommandFromSingleArgument) {
    char* argv[] = { (char*)"kallisto", (char*)"SAVE" };
    std::string command = buildCommandFromArgs(2, argv);
    EXPECT_EQ(command, "SAVE");
}

TEST(KallistoCliTest, BuildCommandFromMultipleArguments) {
    char* argv[] = { (char*)"kallisto", (char*)"MODE", (char*)"BATCH" };
    std::string command = buildCommandFromArgs(3, argv);
    EXPECT_EQ(command, "MODE BATCH");
}

TEST(KallistoCliTest, BuildCommandFromThreeArguments) {
    char* argv[] = { (char*)"kallisto", (char*)"SET", (char*)"KEY", (char*)"VALUE" };
    std::string command = buildCommandFromArgs(4, argv);
    EXPECT_EQ(command, "SET KEY VALUE");
}

// ---------------------------------------------------------------------------
// 2. Help Flag Detection
// ---------------------------------------------------------------------------

TEST(KallistoCliTest, IsHelpRequestedWithLongFlag) {
    EXPECT_TRUE(isHelpRequested("--help"));
}

TEST(KallistoCliTest, IsHelpRequestedWithShortFlag) {
    EXPECT_TRUE(isHelpRequested("-h"));
}

TEST(KallistoCliTest, IsHelpRequestedReturnsFalseForCommands) {
    EXPECT_FALSE(isHelpRequested("SAVE"));
    EXPECT_FALSE(isHelpRequested("MODE"));
    EXPECT_FALSE(isHelpRequested(""));
}

// ---------------------------------------------------------------------------
// 3. Socket Error Handling
// ---------------------------------------------------------------------------

TEST(KallistoCliTest, ConnectToNonExistentSocketFails) {
    // Simulate I/O error: connect to a socket path that doesn't exist
    int result = connectToAdminSocket("/tmp/nonexistent_kallisto_test.sock");
    EXPECT_EQ(result, -1) << "Should fail gracefully when socket doesn't exist";
}

TEST(KallistoCliTest, ReceiveResponseFromInvalidFdReturnsEmpty) {
    // Boundary: recv on an invalid fd should return empty, not crash
    std::string response = receiveResponse(-1);
    EXPECT_TRUE(response.empty());
}

// ---------------------------------------------------------------------------
// 4. RAII SocketGuard
// ---------------------------------------------------------------------------

TEST(KallistoCliTest, SocketGuardClosesFileDescriptor) {
    // Verify RAII cleanup: create a real socket, wrap it, let it go out of scope
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0) << "Should be able to create a socket";

    {
        SocketGuard guard(fd);
        EXPECT_TRUE(guard.isValid());
        EXPECT_EQ(guard.fd(), fd);
    }
    // After guard destructs, the fd should be closed.
    // Attempting to close again should fail with EBADF.
    EXPECT_EQ(::close(fd), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(KallistoCliTest, SocketGuardHandlesInvalidFd) {
    SocketGuard guard(-1);
    EXPECT_FALSE(guard.isValid());
    // Destructor must NOT crash when fd is -1
}
