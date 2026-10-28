# Build handwritten libraries/apps separately from generated Protobuf code.
# Presets export the compiler database consumed by clangd in both editors.
cmake_minimum_required(VERSION 3.24)

project(socket_chat VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(CHAT_ENABLE_SANITIZERS "Build project code with address and undefined-behavior sanitizers" OFF)

find_package(Protobuf CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

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

# Consumers inherit generated-header and dependency include paths from targets.
add_library(chat_proto STATIC "${CHAT_PROTO_SOURCE}" "${CHAT_PROTO_HEADER}")
add_library(chat::proto ALIAS chat_proto)
target_compile_features(chat_proto PUBLIC cxx_std_20)
target_include_directories(chat_proto PUBLIC "${CHAT_GENERATED_DIR}")
target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf)

add_library(chat_framing STATIC src/protocol/framing.cpp)
add_library(chat::framing ALIAS chat_framing)
target_compile_features(chat_framing PUBLIC cxx_std_20)
target_include_directories(chat_framing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(chat_shared INTERFACE)
add_library(chat::shared ALIAS chat_shared)
target_link_libraries(chat_shared INTERFACE chat::proto chat::framing)

# Apply project warnings to handwritten code without flooding generated sources.
function(chat_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

chat_set_project_warnings(chat_framing)

add_library(chat_transport STATIC
    src/transport/tls.cpp
    src/protocol/message_identity.cpp
    src/server/ping_server.cpp
    src/server/client_session.cpp
    src/server/chat_hub.cpp
    src/server/history_store.cpp
    src/client/ping_client.cpp
    src/client/chat_worker.cpp
    src/client/room_model.cpp
    src/ai/ai_engine.cpp
    src/ai/agent_tools.cpp
)
add_library(chat::transport ALIAS chat_transport)
target_link_libraries(chat_transport PUBLIC chat::shared Boost::asio Threads::Threads OpenSSL::SSL)
target_link_libraries(chat_transport PRIVATE unofficial::sqlite3::sqlite3 OpenSSL::Crypto)
if(WIN32)
    target_compile_definitions(chat_transport PUBLIC _WIN32_WINNT=0x0A00)
endif()
chat_set_project_warnings(chat_transport)

add_executable(chat_server apps/server/main.cpp)
add_library(chat_cli_view STATIC apps/cli/chat_console.cpp apps/cli/console_input.cpp)
target_include_directories(chat_cli_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli")
target_link_libraries(chat_cli_view PUBLIC chat::transport)
chat_set_project_warnings(chat_cli_view)
add_executable(chat_cli apps/cli/main.cpp)
target_link_libraries(chat_cli PRIVATE chat_cli_view)
foreach(target IN ITEMS chat_server chat_cli)
    target_link_libraries(${target} PRIVATE chat::transport)
    chat_set_project_warnings(${target})
endforeach()

# The first GUI uses Windows controls; the model/worker remain portable C++.
if(WIN32)
    add_library(chat_gui_view STATIC apps/gui/window.cpp)
    target_include_directories(chat_gui_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui")
    target_link_libraries(chat_gui_view PUBLIC chat::transport user32 gdi32)
    target_compile_definitions(chat_gui_view PUBLIC UNICODE _UNICODE NOMINMAX)
    chat_set_project_warnings(chat_gui_view)
    add_executable(chat_gui WIN32 apps/gui/main.cpp)
    target_link_libraries(chat_gui PRIVATE chat_gui_view shell32)
    chat_set_project_warnings(chat_gui)
endif()

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

    foreach(target IN ITEMS chat_proto chat_framing chat_transport chat_server chat_cli chat_cli_view)
        target_compile_options(${target} PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
        target_link_options(${target} PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
    endforeach()
    target_link_options(chat_shared INTERFACE ${CHAT_SANITIZER_LINK_OPTIONS})
    if(WIN32)
        foreach(target IN ITEMS chat_gui_view chat_gui)
            target_compile_options(${target} PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
            target_link_options(${target} PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
        endforeach()
    endif()
endif()

# CTest groups fast codec tests separately from bounded real-socket scenarios.
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(chat_tests
        tests/unit/framing_test.cpp
        tests/unit/protocol_test.cpp
        tests/unit/room_model_test.cpp
        tests/unit/app_options_test.cpp
        tests/unit/cli_test.cpp
        tests/unit/ai_test.cpp
    )
    target_link_libraries(chat_tests PRIVATE chat_cli_view GTest::gtest_main)
    chat_set_project_warnings(chat_tests)

    if(CHAT_ENABLE_SANITIZERS)
        target_compile_options(chat_tests PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
        target_link_options(chat_tests PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
    endif()

    add_test(NAME chat_tests COMMAND chat_tests)

    add_executable(chat_integration_tests
        tests/integration/ping_test.cpp
        tests/integration/session_test.cpp
        tests/integration/login_test.cpp
        tests/integration/chat_test.cpp
        tests/integration/worker_test.cpp
        tests/integration/history_test.cpp
        tests/integration/heartbeat_test.cpp
        tests/integration/reliability_test.cpp
        tests/integration/recovery_test.cpp
        tests/integration/tls_test.cpp
        tests/integration/cli_test.cpp
        tests/integration/agent_test.cpp
    )
    target_link_libraries(chat_integration_tests PRIVATE chat_cli_view GTest::gtest_main)
    chat_set_project_warnings(chat_integration_tests)
    if(CHAT_ENABLE_SANITIZERS)
        target_compile_options(chat_integration_tests PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
        target_link_options(chat_integration_tests PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
    endif()
    add_test(NAME chat_integration_tests COMMAND chat_integration_tests)
    set_tests_properties(chat_integration_tests PROPERTIES TIMEOUT 45)
    if(WIN32)
        add_executable(chat_gui_tests tests/integration/gui_test.cpp)
        target_link_libraries(chat_gui_tests PRIVATE chat_gui_view GTest::gtest_main)
        chat_set_project_warnings(chat_gui_tests)
        if(CHAT_ENABLE_SANITIZERS)
            target_compile_options(chat_gui_tests PRIVATE ${CHAT_SANITIZER_COMPILE_OPTIONS})
            target_link_options(chat_gui_tests PRIVATE ${CHAT_SANITIZER_LINK_OPTIONS})
        endif()
        add_test(NAME chat_gui_tests COMMAND chat_gui_tests)
        set_tests_properties(chat_gui_tests PROPERTIES TIMEOUT 30)
    endif()
endif()
