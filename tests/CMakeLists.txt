
add_executable(simple simple.c)
add_executable(snapshot snapshot.c)
add_executable(logger logger.c)

target_link_libraries(simple LINK_PUBLIC crate)
target_link_libraries(snapshot LINK_PUBLIC crate)
target_link_libraries(logger LINK_PUBLIC crate)
