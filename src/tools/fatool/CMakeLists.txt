add_executable(fatool
    src/main.c
    src/cmd_ls.c
    src/cmd_cat.c
    src/cmd_cp.c
    src/cmd_mv.c
    src/cmd_rm.c
    src/cmd_mkdir.c
    src/cmd_mount.c
    src/cmd_umount.c
    src/cmd_info.c
)

target_link_libraries(fatool ufs_static)
target_include_directories(fatool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
