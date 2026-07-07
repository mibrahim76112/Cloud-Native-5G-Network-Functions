add_executable(amf_server
    src/main.cpp
    src/amf_server.cpp
)

target_include_directories(amf_server PRIVATE src)

target_link_libraries(amf_server PRIVATE
    n1_proto_lib
    n4_proto_lib
    gRPC::grpc++
    protobuf::libprotobuf
)
