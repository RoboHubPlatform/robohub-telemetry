cmake_minimum_required(VERSION 3.16)
project(robohub-telemetry C)

# ---------------------------------------------------------------------------
# Locating sibling modules
# ---------------------------------------------------------------------------
# Telemetry binds the event bus to transport and net, so it needs all three.
# Paths are never hard-coded: a variable the parent build already set (rho writes
# one for every dependency) wins, then the environment, then a sibling checkout,
# then a fetch.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
endif()

function(robohub_locate_module module out_var)
  string(TOUPPER "${module}" upper)
  string(REPLACE "-" "_" upper "${upper}")
  if(${out_var})
    return()
  endif()
  if(DEFINED ENV{${upper}_DIR})
    set(${out_var} "$ENV{${upper}_DIR}" PARENT_SCOPE)
    return()
  endif()
  set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(level RANGE 6)
    get_filename_component(parent "${dir}/.." ABSOLUTE)
    if(EXISTS "${parent}/${module}/CMakeLists.txt")
      set(${out_var} "${parent}/${module}" PARENT_SCOPE)
      return()
    endif()
    set(dir "${parent}")
  endforeach()
  include(FetchContent)
  message(STATUS "[${PROJECT_NAME}] fetching ${module}")
  FetchContent_Declare(${module}
    GIT_REPOSITORY "https://github.com/RoboHubPlatform/${module}.git"
    GIT_TAG main GIT_SHALLOW TRUE GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(${module})
  set(${out_var} "${${module}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

robohub_locate_module(robohub-common ROBOHUB_COMMON_DIR)
robohub_locate_module(robohub-core ROBOHUB_CORE_DIR)
robohub_locate_module(robohub-transport ROBOHUB_TRANSPORT_DIR)
if(NOT ROBOHUB_COMMON_DIR OR NOT ROBOHUB_CORE_DIR OR NOT ROBOHUB_TRANSPORT_DIR)
  message(FATAL_ERROR "robohub-common, robohub-core or robohub-transport not found and could not be fetched")
endif()
message(STATUS "[${PROJECT_NAME}] robohub-common at ${ROBOHUB_COMMON_DIR}")
message(STATUS "[${PROJECT_NAME}] robohub-core at ${ROBOHUB_CORE_DIR}")
message(STATUS "[${PROJECT_NAME}] robohub-transport at ${ROBOHUB_TRANSPORT_DIR}")

if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory("${ROBOHUB_COMMON_DIR}" robohub-common)
  add_subdirectory("${ROBOHUB_CORE_DIR}" robohub-core)
  add_subdirectory("${ROBOHUB_TRANSPORT_DIR}" robohub-transport)
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(robohub-telemetry STATIC src/telemetry.c)

target_include_directories(robohub-telemetry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ROBOHUB_COMMON_DIR}/include
    ${ROBOHUB_COMMON_DIR}/include/robohub
    ${ROBOHUB_CORE_DIR}/include
    ${ROBOHUB_TRANSPORT_DIR}/include
)
target_link_libraries(robohub-telemetry PUBLIC
    robohub-common
    robohub-core
    robohub-transport)
target_compile_features(robohub-telemetry PUBLIC c_std_11)

# ---------------------------------------------------------------------------
# Host tests
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL AND RHO_BUILD_HOST_TESTS AND NOT RHO_TARGET_IS_BAREMETAL)
  message(STATUS "[${PROJECT_NAME}] registering host unit tests")
  enable_testing()
  add_executable(robohub-telemetry-test tests/test_telemetry.c)
  target_link_libraries(robohub-telemetry-test PRIVATE robohub-telemetry)
  target_include_directories(robohub-telemetry-test PRIVATE "${ROBOHUB_CORE_DIR}/src")
  add_test(NAME robohub-telemetry-test COMMAND robohub-telemetry-test)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(FILES module.yml DESTINATION share/robohub-telemetry)
install(DIRECTORY include/ DESTINATION include)
install(TARGETS robohub-telemetry
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
