add_library(ufs_fat STATIC
    src/fatent.c
    src/cache.c
    src/misc.c
    src/dir.c
    src/vfs_inode.c
    src/vfs_file.c
    src/namei.c
    src/fat_fs.c
)

target_link_libraries(ufs_fat PUBLIC pthread ufs_vfs)

target_include_directories(ufs_fat PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/vfs/src>
)

set_target_properties(ufs_fat PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
