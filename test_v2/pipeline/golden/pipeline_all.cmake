#@@CASE pipeline_positive_links_and_dirs
project(PipeLinks)
add_link_options(-Wl,--as-needed)
link_libraries(m pthread)
include_directories(SYSTEM include)
link_directories(BEFORE lib)
add_library(lib STATIC lib.c)
add_executable(app main.c)
target_link_options(app PRIVATE -Wl,--gc-sections)
target_link_directories(app PRIVATE lib2)
target_link_libraries(app PRIVATE lib)
#@@ENDCASE

#@@CASE pipeline_positive_testing_install
project(PipeTestInstall)
enable_testing()
add_executable(app main.c)
add_test(NAME smoke COMMAND app WORKING_DIRECTORY . COMMAND_EXPAND_LISTS)
add_test(legacy app)
install(TARGETS app DESTINATION bin)
install(FILES readme.txt DESTINATION share/doc)
install(PROGRAMS run.sh DESTINATION bin)
install(DIRECTORY assets DESTINATION share/assets)
#@@ENDCASE

#@@CASE pipeline_positive_cpack
project(PipePack)
cpack_add_install_type(Full DISPLAY_NAME "Full Install")
cpack_add_component_group(base DISPLAY_NAME "Base" DESCRIPTION "Base Group")
cpack_add_component(core DISPLAY_NAME "Core" GROUP base INSTALL_TYPES Full REQUIRED)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_group
project(BadPackGroup)
cpack_add_component(core GROUP missing)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_dependency
project(BadPackDepends)
cpack_add_install_type(Full)
cpack_add_component(core DEPENDS missing INSTALL_TYPES Full)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_install_type
project(BadPackInstallType)
cpack_add_component(core INSTALL_TYPES MissingType)
#@@ENDCASE

#@@CASE pipeline_negative_install_missing_destination
project(BadInstall)
install(FILES foo.txt)
#@@ENDCASE
