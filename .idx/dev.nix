# To learn more about how to use Nix to configure your environment
# see: https://firebase.google.com/docs/studio/customize-workspace
{ pkgs, ... }: {
  # Which nixpkgs channel to use.
  channel = "stable-24.05"; # or "unstable"

  # Use https://search.nixos.org/packages to find packages
  packages = [
    # C++ Toolchain
    pkgs.gcc13 # Using a specific version is good practice
    pkgs.gdb
    pkgs.cmake
    pkgs.pkg-config
    pkgs.gnumake # Đã sửa từ pkgs.make thành pkgs.gnumake

    # Project dependencies/tools (for protoc compiler, etc.)
    pkgs.protobuf
    pkgs.grpc

    # Benchmarking tools mentioned in the repository
    pkgs.ghz
    pkgs.wrk
  ];

  # Set environment variables
  env = {
    # This is crucial for CMake to find and use vcpkg.
    VCPKG_ROOT = "$HOME/vcpkg";
    # Instruct vcpkg to use the manifest file (vcpkg.json) by default.
    VCPKG_FEATURE_FLAGS = "manifests";
  };

  # Gom tất cả cấu hình đặc thù của Firebase Studio/IDX vào block 'idx'
  idx = {
    workspace = {
      # Run script on workspace start
      onCreate = {
        # This script clones and bootstraps vcpkg if it's not already present.
        "01-bootstrap-vcpkg" = ''
          if [ ! -f "$HOME/vcpkg/vcpkg" ]; then
            echo "Cloning and bootstrapping vcpkg..."
            # Using a shallow clone to speed up the process
            git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
            "$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
          else
            echo "vcpkg is already available."
          fi
        '';
        # Provide clear instructions to the user on how to proceed.
        "02-user-guidance" = ''
          echo -e "\n####################################################"
          echo -e "###                                                ###"
          echo -e "###   Your C++ development environment is ready!   ###"
          echo -e "###                                                ###"
          echo -e "####################################################\n"
          echo "This project uses vcpkg with a manifest (vcpkg.json) for dependencies."
          echo "They will be automatically installed on the first build."
          echo ""
          echo "To build the project, simply run:"
          echo "  make"
          echo ""
        '';
      };
    };

    # Cấu trúc Preview chuẩn của IDX
    previews = {
      enable = true;
      previews = {
        kallisto-http = {
          # Firebase Studio previews yêu cầu một command để chạy server
          # Bạn có thể đổi command này thành lệnh start server HTTP của bạn
          command = ["echo" "Chạy HTTP Server tại đây..."]; 
          env = { PORT = "8080"; };
          manager = "web";
        };
        kallisto-grpc = {
          command = ["echo" "Chạy gRPC Server tại đây..."];
          env = { PORT = "50051"; };
          manager = "web";
        };
      };
    };
  };
}