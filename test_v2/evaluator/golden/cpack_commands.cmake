project(CPackDemo VERSION 2.0)
cpack_add_install_type(Full DISPLAY_NAME "Full")
cpack_add_component(core DISPLAY_NAME "Core")
add_executable(pkg_app main.c)