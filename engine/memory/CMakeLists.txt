# aegis_memory — VRAM allocator & transfer queue (Milestone 2)
add_library(aegis_memory STATIC
    src/free_list.cpp
    src/gpu_buffer.cpp
    src/buffer_arena.cpp
    src/staging_ring.cpp
    src/transfer_queue.cpp)
add_library(aegis::memory ALIAS aegis_memory)

target_include_directories(aegis_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_memory PUBLIC aegis::vk)
aegis_set_warnings(aegis_memory)
