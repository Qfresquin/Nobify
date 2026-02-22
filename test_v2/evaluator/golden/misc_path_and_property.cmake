project(MiscDemo)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
cmake_path(SET P NORMALIZE "a/./b/../c")
add_executable(misc_app main.c)