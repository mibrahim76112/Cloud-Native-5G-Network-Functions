cmake_minimum_required(VERSION 3.16)
project(5g_cloud_native_nf CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

message(STATUS "Protobuf version: ${Protobuf_VERSION}")
message(STATUS "gRPC version: ${gRPC_VERSION}")

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_OUT "${CMAKE_BINARY_DIR}/proto_gen")
file(MAKE_DIRECTORY ${PROTO_OUT})

get_target_property(PROTOC_BIN protobuf::protoc IMPORTED_LOCATION_RELEASE)
if(NOT PROTOC_BIN)
  get_target_property(PROTOC_BIN protobuf::protoc IMPORTED_LOCATION_NOCONFIG)
endif()

get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
if(NOT GRPC_CPP_PLUGIN)
  get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_NOCONFIG)
endif()

set(N1_PROTO "${PROTO_DIR}/n1.proto")
set(N1_SOURCES
    "${PROTO_OUT}/n1.pb.cc"
    "${PROTO_OUT}/n1.grpc.pb.cc"
)
set(N1_HEADERS
    "${PROTO_OUT}/n1.pb.h"
    "${PROTO_OUT}/n1.grpc.pb.h"
)

add_custom_command(
  OUTPUT ${N1_SOURCES} ${N1_HEADERS}
  COMMAND ${PROTOC_BIN}
    --proto_path=${PROTO_DIR}
    --cpp_out=${PROTO_OUT}
    --grpc_out=${PROTO_OUT}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    ${N1_PROTO}
  DEPENDS ${N1_PROTO}
  COMMENT "Generating gRPC stubs from n1.proto"
)

set(N4_PROTO "${PROTO_DIR}/n4.proto")
set(N4_SOURCES
    "${PROTO_OUT}/n4.pb.cc"
    "${PROTO_OUT}/n4.grpc.pb.cc"
)
set(N4_HEADERS
    "${PROTO_OUT}/n4.pb.h"
    "${PROTO_OUT}/n4.grpc.pb.h"
)

add_custom_command(
  OUTPUT ${N4_SOURCES} ${N4_HEADERS}
  COMMAND ${PROTOC_BIN}
    --proto_path=${PROTO_DIR}
    --cpp_out=${PROTO_OUT}
    --grpc_out=${PROTO_OUT}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    ${N4_PROTO}
  DEPENDS ${N4_PROTO}
  COMMENT "Generating gRPC stubs from n4.proto"
)

add_library(n1_proto_lib STATIC ${N1_SOURCES} ${N1_HEADERS})
target_include_directories(n1_proto_lib PUBLIC ${PROTO_OUT})
target_link_libraries(n1_proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_library(n4_proto_lib STATIC ${N4_SOURCES} ${N4_HEADERS})
target_include_directories(n4_proto_lib PUBLIC ${PROTO_OUT})
target_link_libraries(n4_proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_subdirectory(amf)
add_subdirectory(upf)
add_subdirectory(ue-sim)
