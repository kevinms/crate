
add_executable(main main.c)
add_executable(small small.c)

target_link_libraries(main LINK_PUBLIC crate)
target_link_libraries(small LINK_PUBLIC crate)
