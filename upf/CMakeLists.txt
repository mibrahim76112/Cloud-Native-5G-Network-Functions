add_executable(upf_server
    src/main.cpp
    src/upf_server.cpp
)

target_include_directories(upf_server PRIVATE src)

target_link_libraries(upf_server PRIVATE
    n4_proto_lib
    gRPC::grpc++
    protobuf::libprotobuf
)
