cmake_minimum_required(VERSION 3.16)
project(userfs C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(BUILD_TOOLS "Build CLI tools" ON)
option(BUILD_SHARED "Build shared library" ON)

# Sources
set(VFS_SOURCES
    src/vfs/block_io.c
    src/vfs/super.c
    src/vfs/inode.c
    src/vfs/dentry.c
    src/vfs/path.c
    src/vfs/file.c
    src/vfs/register.c
    src/vfs/api.c
)

set(FAT_SOURCES
    src/fat/fatent.c
    src/fat/cache.c
    src/fat/misc.c
    src/fat/dir.c
    src/fat/vfs_inode.c
    src/fat/vfs_file.c
    src/fat/namei.c
    src/fat/fat_fs.c
)

set(CORE_SOURCES
    src/libufs.c
    ${VFS_SOURCES}
    ${FAT_SOURCES}
)

# Include path
include_directories(include)
include_directories(src/vfs)
include_directories(src/fat)

# Build library
if(BUILD_SHARED)
    add_library(ufs SHARED ${CORE_SOURCES})
    target_link_libraries(ufs pthread)
    set_target_properties(ufs PROPERTIES
        OUTPUT_NAME ufs
        SOVERSION 1
    )
endif()

add_library(ufs_static STATIC ${CORE_SOURCES})
target_link_libraries(ufs_static pthread)
set_target_properties(ufs_static PROPERTIES OUTPUT_NAME ufs)

# CLI tools
if(BUILD_TOOLS)
    add_subdirectory(tools/fatool)
endif()

# Install
install(TARGETS ufs ufs_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)


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
