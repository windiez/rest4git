cmake_minimum_required(VERSION 3.2)
project (rest4git LANGUAGES CXX)

option(ENABLE_LIBGIT2 "Use libgit2 optionally if available" ON)
option(ENABLE_OPENMP  "Use openmp parallelisation optionally if available" ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Tcmalloc)
find_package(Threads)
find_package(OpenSSL)
if(ENABLE_LIBGIT2)
  find_package(libgit2)
  if(LIBGIT2_FOUND)
    include_directories(${GIT2_INCLUDE_PATH})
  else(LIBGIT2_FOUND)
    option(ENABLE_LIBGIT2 "Use libgit2 optionally if available" OFF)
  endif(LIBGIT2_FOUND)
endif(ENABLE_LIBGIT2)

if(OPENSSL_FOUND)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif()

find_package( Boost 1.52 COMPONENTS system thread REQUIRED )

include_directories( ${Boost_INCLUDE_DIR} )

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message("Found ccache ${CCACHE_FOUND}")
  message("Using ccache to speed up compilation")
  set(ENV{CCACHE_CPP2} "yes")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

if(ENABLE_OPENMP)
  find_package(OpenMP)
  if(OPENMP_FOUND)
    include_directories(${GIT2_INCLUDE_PATH})
  else(OPENMP_FOUND)
    option(ENABLE_OPENMP  "Use openmp parallelisation optionally if available" OFF)
  endif(OPENMP_FOUND)
endif(ENABLE_OPENMP)

if (NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release")
endif()

string(
    APPEND opts
        "$<$<CONFIG:RELEASE>:-Wall;-Ofast;-std=c++17;-march=native;-fopenmp;-D_GLIBCXX_PARALLEL>"
        "$<$<CONFIG:DEBUG>:-Wall;-O0;-g3;-std=c++17;-frecord-gcc-switches;-D_GLIBCXX_DEBUG>"
)

add_compile_options("${opts}")

add_definitions(-DCROW_USE_BOOST)
add_executable(rest4git src/main.cpp src/git2api.cpp)
target_include_directories(rest4git PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

include(build_hash.cmake)
target_compile_definitions(rest4git 
  PRIVATE REST4GIT_BUILD_HASH="${rest4git_HASH}")

target_link_libraries(rest4git ${Boost_LIBRARIES})

if (THREADS_FOUND)
  target_link_libraries(rest4git ${CMAKE_THREAD_LIBS_INIT})
endif(THREADS_FOUND)

if (LIBGIT2_FOUND)
  target_link_libraries(rest4git ${GIT2_LIBRARY})
  add_definitions(-DLIBGIT2_AVAILABLE)
endif(LIBGIT2_FOUND)

if (OPENSSL_FOUND)
  target_link_libraries(rest4git ${OPENSSL_LIBRARIES})
endif(OPENSSL_FOUND)

if (Tcmalloc_FOUND)
  target_link_libraries(rest4git ${Tcmalloc_LIBRARIES})
endif(Tcmalloc_FOUND)

if(OPENMP_FOUND)
  target_link_libraries(rest4git OpenMP::OpenMP_CXX)
endif(OPENMP_FOUND)