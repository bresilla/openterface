cmake_minimum_required(VERSION 3.15)

set(project_name openterface)
project(${project_name} VERSION 0.1.0 LANGUAGES C CXX)

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(params
    -Wno-reorder
    -Wno-unused-variable
    -Wno-unused-parameter
)

option(RERUN_DOWNLOAD_AND_BUILD_ARROW "Download and build arrow" OFF)

string(TOUPPER ${project_name} project_name_upper)
option(${project_name_upper}_BUILD_EXAMPLES "Build examples" OFF)
option(${project_name_upper}_ENABLE_TESTS "Enable tests" OFF)
include(FetchContent)

# --------------------------------------------------------------------------------------------------
# External Dependencies
set(ext_deps)
# Add external dependencies here and append them to `ext_deps`
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG main
)
FetchContent_MakeAvailable(cli11)
list(APPEND ext_deps CLI11::CLI11)

# Find Wayland
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_CURSOR REQUIRED wayland-cursor)
list(APPEND ext_deps ${WAYLAND_CLIENT_LIBRARIES})
list(APPEND ext_deps ${WAYLAND_CURSOR_LIBRARIES})



# --------------------------------------------------------------------------------------------------
add_library(${project_name} INTERFACE)
# Allow users to link via `${project_name}::${project_name}`
add_library(${project_name}::${project_name} ALIAS ${project_name})
target_include_directories(${project_name} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

install(
  DIRECTORY include/
  DESTINATION include
)
include_directories(include)

# --------------------------------------------------------------------------------------------------
include(GNUInstallDirs)

#Install headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#Install and export the INTERFACE target
install(TARGETS ${project_name} EXPORT ${project_name}Targets)

#Install the export file so that `find_package(${project_name})` works
install(EXPORT ${project_name}Targets
  FILE ${project_name}Targets.cmake
  NAMESPACE ${project_name}::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${project_name}
)

#Also export it for in‐source FetchContent usage
export(EXPORT ${project_name}Targets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/${project_name}Targets.cmake"
  NAMESPACE ${project_name}::
)


# --------------------------------------------------------------------------------------------------
file(GLOB_RECURSE internal_deps
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Add the xdg-shell protocol implementation (C file)
set(XDG_SHELL_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/src/xdg-shell-protocol.c")
list(APPEND internal_deps ${XDG_SHELL_PROTOCOL})

# --------------------------------------------------------------------------------------------------
if(${project_name_upper}_BUILD_EXAMPLES)
  file(GLOB exe examples/*.cpp)
  # ----------------------------------------------
  set(exec_names)
  foreach(src_file IN LISTS exe)
    get_filename_component(exec_name "${src_file}" NAME_WE)
    add_executable(${exec_name} "${src_file}")
      target_compile_options(${exec_name} PRIVATE ${params})
      foreach(lib_file IN LISTS internal_deps)
        target_compile_options(${exec_name} PRIVATE ${params})
        target_sources(${exec_name} PRIVATE "${lib_file}")
      endforeach()
    target_link_libraries(${exec_name} ${ext_deps})
    install(TARGETS ${exec_name} DESTINATION bin)
    list(APPEND exec_names ${exec_name})
  endforeach()
  # ----------------------------------------------
  foreach(exec IN LISTS exec_names)
    file(REMOVE "${CMAKE_CURRENT_LIST_DIR}/.execs")
    file(WRITE "${CMAKE_CURRENT_LIST_DIR}/.execs")
    get_filename_component(exec_name "${exec}" NAME)
    file(APPEND "${CMAKE_CURRENT_LIST_DIR}/.execs" "${CMAKE_CURRENT_LIST_DIR}/build/${exec_name}\n")
  endforeach()
  # ----------------------------------------------
  install(TARGETS
    ${exec_names}
    DESTINATION lib/${PROJECT_NAME}
  )
endif()


# --------------------------------------------------------------------------------------------------
if(${project_name_upper}_ENABLE_TESTS)
  enable_testing()
  add_definitions(-DENABLE_DOCTEST_IN_LIBRARY)
  include(FetchContent)
  FetchContent_Declare(DocTest GIT_REPOSITORY "https://github.com/onqtam/doctest")
  FetchContent_MakeAvailable(DocTest)

  file(GLOB test_src test/*.cpp)

  foreach(src_file IN LISTS test_src)
    get_filename_component(test_name "${src_file}" NAME_WE)
    add_executable(${test_name} "${src_file}")
      target_compile_options(${test_name} PRIVATE ${params})
      foreach(lib_file IN LISTS internal_deps)
        target_sources(${test_name} PRIVATE "${lib_file}")
      endforeach()
    target_link_libraries(${test_name} ${ext_deps} doctest_with_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

