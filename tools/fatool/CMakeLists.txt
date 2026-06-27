add_executable(fatool
    main.c
    cmd_ls.c
    cmd_cat.c
    cmd_cp.c
    cmd_mv.c
    cmd_rm.c
    cmd_mkdir.c
    cmd_mount.c
    cmd_umount.c
    cmd_info.c
)

target_link_libraries(fatool ufs_static)
target_include_directories(fatool PRIVATE ${CMAKE_SOURCE_DIR}/include)
