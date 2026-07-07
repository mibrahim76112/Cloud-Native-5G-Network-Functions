FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    libssl-dev libz-dev autoconf libtool \
    && rm -rf /var/lib/apt/lists/*

ENV GRPC_VERSION=v1.60.0
RUN git clone --recurse-submodules -b ${GRPC_VERSION} \
    https://github.com/grpc/grpc /tmp/grpc && \
    cmake -S /tmp/grpc -B /tmp/grpc/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_SSL_PROVIDER=package && \
    cmake --build /tmp/grpc/build -j$(nproc) && \
    cmake --install /tmp/grpc/build && \
    rm -rf /tmp/grpc

WORKDIR /src
COPY proto/ proto/
COPY amf/ amf/
COPY upf/ upf/
COPY ue-sim/ ue-sim/
COPY CMakeLists.txt .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target ue_sim -j$(nproc)

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libgrpc*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprotobuf*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libabsl*.so* /usr/local/lib/
COPY --from=builder /src/build/ue-sim/ue_sim /usr/local/bin/ue_sim

RUN ldconfig
ENTRYPOINT ["/usr/local/bin/ue_sim"]
