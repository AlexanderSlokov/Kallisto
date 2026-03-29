FROM mcr.microsoft.com/devcontainers/cpp:ubuntu-24.04

# 1. Cài thêm 'sudo' và các gói cần thiết
RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    cmake \
    gdb \
    git \
    python3 \
    python3-pip \
    python3-venv \
    valgrind \
    wget \
    curl \
    procps \
    sudo \
    ca-certificates \
    libspdlog-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# [Kallisto] Create data directory
RUN mkdir -p /data/kallisto && chown -R vscode:vscode /data/kallisto

USER vscode

# [Kallisto] Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg" && cd "$HOME/vcpkg" && ./bootstrap-vcpkg.sh
RUN export VCPKG_ROOT="$HOME/vcpkg" && export PATH="$VCPKG_ROOT:$PATH"

# [Kallisto] Install wrk
RUN git clone https://github.com/wg/wrk.git wrk && cd wrk && sudo make && sudo cp wrk /usr/local/bin
