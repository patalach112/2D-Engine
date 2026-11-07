# aegis_memory — VRAM allocator & transfer queue (Milestone 2)
add_library(aegis_memory OBJECT
    src/free_list.cpp
    src/gpu_buffer.cpp
    src/buffer_arena.cpp
    src/staging_ring.cpp
    src/transfer_queue.cpp)
aegis_engine_module(aegis_memory)
