project(TransperentProxy C)

set(THREADS_PREFER_PTHREAD_FLAG ON)

file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -pipe -Wall -Wextra -Wno-error=unused-parameter -fdata-sections -ffunction-sections -Wl,--gc-sections")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fanalyzer -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "-Ofast -DNDEBUG")

add_executable(TransperentProxy ${SOURCES} ${HEADERS})

find_package(Threads REQUIRED)
target_link_libraries(TransperentProxy Threads::Threads)
