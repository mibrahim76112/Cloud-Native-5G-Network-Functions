add_executable(ue_sim
    main.cpp
)

target_link_libraries(ue_sim PRIVATE
    n1_proto_lib
    n4_proto_lib
    gRPC::grpc++
    protobuf::libprotobuf
)
