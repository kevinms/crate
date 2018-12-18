
add_executable(simple simple.c)
add_executable(snapshot snapshot.c)

target_link_libraries(simple LINK_PUBLIC crate)
target_link_libraries(snapshot LINK_PUBLIC crate)
