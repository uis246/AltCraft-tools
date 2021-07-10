project(MMGen C)

set(THREADS_PREFER_PTHREAD_FLAG ON)

file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -O1 -fno-plt -pipe -Wall -Wextra -Wno-error=unused-parameter -fdata-sections -ffunction-sections -Wl,--gc-sections")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE "-Ofast -DNDEBUG")

add_executable(MMGen ${SOURCES} ${HEADERS})

#find_package(Threads REQUIRED)
#target_link_libraries(TransperentProxy Threads::Threads)

install(TARGETS MMGen RUNTIME DESTINATION bin)

find_package(PNG REQUIRED)
target_link_libraries(MMGen ${PNG_LIBRARY})
target_include_directories(MMGen PUBLIC ${PNG_INCLUDE_DIRS})
