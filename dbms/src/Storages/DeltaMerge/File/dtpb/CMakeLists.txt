protobuf_generate_cpp(
    dtpb_srcs dtpb_hdrs
    dmfile.proto
)

add_library(dtpb ${dtpb_srcs})
target_include_directories(dtpb PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS})
target_compile_options(dtpb
    PRIVATE -Wno-unused-parameter
)
