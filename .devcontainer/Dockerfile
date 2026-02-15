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

RUN pip3 install locust --break-system-packages

# # 2. Tạo user vscode và cấp quyền SUDO (Quan trọng để tránh lỗi 127)
# RUN useradd -ms /bin/bash vscode \
#     && echo "vscode ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/vscode \
#     && chmod 0440 /etc/sudoers.d/vscode

# [Kallisto] Tạo thư mục data
RUN mkdir -p /data/kallisto && chown -R vscode:vscode /data/kallisto

USER vscode

# # 3. Đổi Workdir về /workspaces (nơi code được mount vào)
# WORKDIR /workspaces