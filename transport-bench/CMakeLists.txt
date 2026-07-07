# REST AMF server
add_executable(rest_amf_server rest_amf_server.cpp)
target_include_directories(rest_amf_server PRIVATE .)
target_link_libraries(rest_amf_server PRIVATE pthread)

# Transport benchmark client
add_executable(bench_compare bench_compare.cpp)
target_include_directories(bench_compare PRIVATE .)
target_link_libraries(bench_compare PRIVATE
    n1_proto_lib
    gRPC::grpc++
    protobuf::libprotobuf
    pthread
)
