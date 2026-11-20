cmake_minimum_required(VERSION 3.22)
project(ax-lang-native VERSION 2.0.0 LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
  add_compile_options(-march=armv8.2-a+fp16+dotprod -O3 -Wall -Wextra)
  file(GLOB AXRT_ASM "runtime/coro/*.S")
  list(APPEND AXRT_ASM "compiler/backends/arm64/neon_dot.S")
else()
  add_compile_options(-O2 -Wall -Wextra)
  # These assembly sources use AArch64 instructions and cannot be assembled on
  # other hosts. The C++ compiler and code generators remain buildable there.
  set(AXRT_ASM "")
endif()
add_compile_options(-Wno-unused-parameter -Wno-unused-variable)
include_directories(${CMAKE_SOURCE_DIR})
# libaxrt - runtime
file(GLOB AXRT_SOURCES "runtime/core/*.cpp" "runtime/coro/*.cpp" "runtime/dist/*.cpp" "runtime/core/*.c")
add_library(axrt STATIC ${AXRT_SOURCES} ${AXRT_ASM})
target_include_directories(axrt PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(axrt PUBLIC m dl pthread)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  target_compile_definitions(axrt PUBLIC AX_HAVE_VULKAN=1)
  target_link_libraries(axrt PUBLIC Vulkan::Vulkan)
else()
  target_compile_definitions(axrt PUBLIC AX_HAVE_VULKAN=0)
endif()
set_target_properties(axrt PROPERTIES POSITION_INDEPENDENT_CODE ON)
# axc core library
file(GLOB AXC_LIB_SOURCES "compiler/common/*.cpp" "compiler/lexer/*.cpp" "compiler/parser/*.cpp" "compiler/sema/*.cpp" "compiler/ir/*.cpp" "compiler/opt/*.cpp" "compiler/backends/*/*.cpp" "compiler/backends/*/*.c")
add_library(axc_core STATIC ${AXC_LIB_SOURCES})
target_include_directories(axc_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(axc_core PUBLIC axrt m dl pthread)
add_library(ax_embed STATIC compiler/embed/embed.cpp)
target_include_directories(ax_embed PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ax_embed PUBLIC axc_core axrt)
# axc binary
add_executable(axc tools/axc/main.cpp tools/axc/cli.cpp)
target_link_libraries(axc PRIVATE axc_core axrt)
# Short command name for interactive use; keep axc for existing scripts.
add_executable(ax tools/axc/main.cpp tools/axc/cli.cpp)
target_link_libraries(ax PRIVATE axc_core axrt)
# tools
add_executable(axfmt tools/axfmt/main.cpp)
target_link_libraries(axfmt PRIVATE axc_core)
add_executable(axlint tools/axlint/main.cpp)
target_link_libraries(axlint PRIVATE axc_core)
add_executable(axpkg tools/axpkg/main.cpp)
add_executable(axlsp tools/axlsp/main.cpp)
target_link_libraries(axlsp PRIVATE axc_core)
add_executable(axdbg tools/axdbg/main.cpp)
target_link_libraries(axdbg PRIVATE axc_core axrt)
# Native AX source editor with syntax coloring, type hints, and completions.
add_executable(axedit tools/axedit/main.cpp)
target_link_libraries(axedit PRIVATE axc_core)
# tests
add_executable(ax_tests tests/test_runner.cpp)
target_link_libraries(ax_tests PRIVATE axc_core axrt)
add_executable(ax_m2_ai_tests tests/test_ai_m2.cpp)
target_link_libraries(ax_m2_ai_tests PRIVATE axrt)
add_executable(ax_m3_tests tests/test_m3.cpp)
target_link_libraries(ax_m3_tests PRIVATE axrt)
add_executable(ax_m4_tests tests/test_m4.cpp)
target_link_libraries(ax_m4_tests PRIVATE axrt)
add_executable(ax_embed_c_test tests/test_embed_c.c)
target_include_directories(ax_embed_c_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(ax_embed_c_test PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(ax_embed_c_test PRIVATE ax_embed)
enable_testing()
add_test(NAME ax_unit_tests COMMAND ax_tests)
add_test(NAME ax_m2_ai_tests COMMAND ax_m2_ai_tests)
add_test(NAME ax_m3_tests COMMAND ax_m3_tests)
add_test(NAME ax_m4_tests COMMAND ax_m4_tests)
add_test(NAME ax_embed_c_api COMMAND ax_embed_c_test)
add_test(NAME ax_examples
  COMMAND ${CMAKE_COMMAND} -E env AXC=$<TARGET_FILE:axc>
          bash ${CMAKE_SOURCE_DIR}/scripts/run_tests.sh)
add_test(NAME ax_cook_cli
  COMMAND ${CMAKE_COMMAND} -E env AXC=$<TARGET_FILE:axc>
          bash ${CMAKE_SOURCE_DIR}/tests/test_cook_cli.sh)
add_test(NAME axpkg_lock_cli
  COMMAND ${CMAKE_COMMAND} -E env AXPKG=$<TARGET_FILE:axpkg>
          bash ${CMAKE_SOURCE_DIR}/tests/test_axpkg_lock.sh)
add_test(NAME ax_shared_cli
  COMMAND ${CMAKE_COMMAND} -E env AXC=$<TARGET_FILE:axc>
          CC=${CMAKE_C_COMPILER} AX_INCLUDE=${CMAKE_SOURCE_DIR}/include
          bash ${CMAKE_SOURCE_DIR}/tests/test_shared_cli.sh)
set_tests_properties(ax_examples PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(ax_cook_cli PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(axpkg_lock_cli PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(ax_shared_cli PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Installable Linux distribution: compiler/tools, editor, development archives,
# public headers, standard-library sources, examples, and local documentation.
include(GNUInstallDirs)
install(TARGETS ax axc axedit axfmt axlint axpkg axlsp axdbg
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS axrt axc_core ax_embed
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ax)
install(DIRECTORY compiler/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ax/compiler
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY runtime/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ax/runtime
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY stdlib/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/ax/stdlib)
install(DIRECTORY examples/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/ax/examples
  FILES_MATCHING PATTERN "*.ax")
install(DIRECTORY docs/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/ax/docs
  FILES_MATCHING PATTERN "*.md" PATTERN "*.css" PATTERN "*.js")
install(FILES README.md LICENSE MANIFEST.md axpkg.toml index.html
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ax)

set(CPACK_PACKAGE_NAME "ax-lang-native")
set(CPACK_PACKAGE_VENDOR "AX language project")
set(CPACK_PACKAGE_CONTACT "Ansh Raj")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AX native compiler, editor, runtime, and development tools")
set(CPACK_PACKAGE_DESCRIPTION "Experimental indentation-sensitive language with a native C++20 compiler, AX terminal editor, and optional Vulkan compute backend.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ansh Raj")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_GENERATOR "DEB")
include(CPack)
