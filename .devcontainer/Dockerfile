FROM ubuntu:24.04

# Cập nhật & cài các công cụ cần thiết
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
    ca-certificates \
    libspdlog-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Cài đặt Locust system-wide để dùng được luôn trong terminal
RUN pip3 install locust --break-system-packages

# Thêm user "vscode" cho môi trường phát triển
RUN useradd -ms /bin/bash vscode

# [Kallisto] Tạo thư mục data và setup quyền cho user vscode
RUN mkdir -p /data/kallisto && chown -R vscode:vscode /data/kallisto

USER vscode
WORKDIR /workspaces/kallisto
