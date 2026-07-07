FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    pkg-config \
    libssl-dev \
    libz-dev \
    autoconf \
    libtool \
    clang \
    && rm -rf /var/lib/apt/lists/*

ENV GRPC_VERSION=v1.60.0
ENV GRPC_INSTALL_DIR=/usr/local

RUN git clone --recurse-submodules -b ${GRPC_VERSION} \
    https://github.com/grpc/grpc /tmp/grpc

RUN cmake -S /tmp/grpc -B /tmp/grpc/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${GRPC_INSTALL_DIR} \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_SSL_PROVIDER=package \
    && cmake --build /tmp/grpc/build -j$(nproc) \
    && cmake --install /tmp/grpc/build

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libssl-dev \
    libz-dev \
    gdb \
    valgrind \
    curl \
    wget \
    vim \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local /usr/local

RUN ldconfig

WORKDIR /workspace