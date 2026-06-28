add_library(ufs_vfs STATIC
    src/block_io.c
    src/super.c
    src/inode.c
    src/dentry.c
    src/path.c
    src/file.c
    src/register.c
    src/api.c
)

target_link_libraries(ufs_vfs PUBLIC pthread)

target_include_directories(ufs_vfs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(ufs_vfs PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

set_target_properties(ufs_vfs PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
