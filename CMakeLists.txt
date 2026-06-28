cmake_minimum_required(VERSION 3.16)
project(userfs C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(BUILD_TOOLS "Build CLI tools" ON)
option(BUILD_SHARED "Build shared library" ON)

# Sub-libraries
add_subdirectory(src/vfs)
add_subdirectory(src/fs/fat)

# ---- Combined shared library ----
if(BUILD_SHARED)
    add_library(ufs SHARED src/libufs.c)
    target_link_libraries(ufs PRIVATE ufs_vfs ufs_fat)
    set_target_properties(ufs PROPERTIES
        SOVERSION 1
    )
endif()

# ---- Combined static library (links the sub-libraries) ----
add_library(ufs_static STATIC src/libufs.c)
target_link_libraries(ufs_static PUBLIC ufs_vfs ufs_fat)
set_target_properties(ufs_static PROPERTIES OUTPUT_NAME ufs)

# CLI tools
if(BUILD_TOOLS)
    add_subdirectory(src/tools/fatool)
endif()

# Install
install(TARGETS ufs_vfs ufs_fat
    ARCHIVE DESTINATION lib
)
install(TARGETS ufs ufs_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY src/vfs/include/ DESTINATION include)


if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  add_custom_target(sync_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/build"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
    COMMENT "Sync compile_commands.json to build/compile_commands.json"
    VERBATIM
  )
  add_dependencies(sync_compile_commands fatool ufs_static)
endif()
