cmake_minimum_required(VERSION 3.24)

project(socket_chat VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(CHAT_ENABLE_SANITIZERS "Build project code with address and undefined-behavior sanitizers" OFF)

find_package(Protobuf CONFIG REQUIRED)

set(CHAT_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/chat.proto")
set(CHAT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CHAT_PROTO_SOURCE "${CHAT_GENERATED_DIR}/chat.pb.cc")
set(CHAT_PROTO_HEADER "${CHAT_GENERATED_DIR}/chat.pb.h")

add_custom_command(
    OUTPUT "${CHAT_PROTO_SOURCE}" "${CHAT_PROTO_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CHAT_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
            "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto"
            "--cpp_out=${CHAT_GENERATED_DIR}"
            "${CHAT_PROTO_FILE}"
    DEPENDS "${CHAT_PROTO_FILE}" protobuf::protoc
    COMMENT "Generating C++ sources from proto/chat.proto"
    VERBATIM
)

add_library(chat_proto STATIC "${CHAT_PROTO_SOURCE}" "${CHAT_PROTO_HEADER}")
add_library(chat::proto ALIAS chat_proto)
target_compile_features(chat_proto PUBLIC cxx_std_20)
target_include_directories(chat_proto PUBLIC "${CHAT_GENERATED_DIR}")
target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf)

add_library(chat_framing STATIC shared/src/framing.cpp)
add_library(chat::framing ALIAS chat_framing)
target_compile_features(chat_framing PUBLIC cxx_std_20)
target_include_directories(chat_framing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/shared/include")

add_library(chat_shared INTERFACE)
add_library(chat::shared ALIAS chat_shared)
target_link_libraries(chat_shared INTERFACE chat::proto chat::framing)

function(chat_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

chat_set_project_warnings(chat_framing)

if(CHAT_ENABLE_SANITIZERS)
    if(MSVC)
        set(CHAT_SANITIZER_COMPILE_OPTIONS /fsanitize=address /Zi)
        set(CHAT_SANITIZER_LINK_OPTIONS /fsanitize=address)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(CHAT_SANITIZER_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
        set(CHAT_SANITIZER_LINK_OPTIONS -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "Sanitizers are unsupported by ${CMAKE_CXX_COMPILER_ID}")
    endif()

    foreach(target IN ITEMS chat_proto chat_framing)
        target_compile_options(${target} PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
        target_link_options(${target} PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
    endforeach()
    target_link_options(chat_shared INTERFACE ${CHAT_SANITIZER_LINK_OPTIONS})
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(chat_tests
        tests/framing_test.cpp
        tests/protocol_test.cpp
    )
    target_link_libraries(chat_tests PRIVATE chat::shared GTest::gtest_main)
    chat_set_project_warnings(chat_tests)

    if(CHAT_ENABLE_SANITIZERS)
        target_compile_options(chat_tests PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
        target_link_options(chat_tests PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
    endif()

    add_test(NAME chat_tests COMMAND chat_tests)
endif()
