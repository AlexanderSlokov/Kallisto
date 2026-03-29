set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Use system OpenSSL instead of building from source
set(VCPKG_POLICY_ALLOW_SYSTEM_LIBS enabled)
