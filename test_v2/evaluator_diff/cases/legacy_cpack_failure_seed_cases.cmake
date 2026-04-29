#@@CASE legacy_cpack_component_supported_surface
#@@OUTCOME SUCCESS
include(CPackComponent)
cpack_add_install_type(full DISPLAY_NAME "Full")
cpack_add_component_group(base DISPLAY_NAME "Base" DESCRIPTION "Base group" EXPANDED BOLD_TITLE)
cpack_add_component(core DISPLAY_NAME "Core" DESCRIPTION "Core component" GROUP base DEPENDS dep INSTALL_TYPES full ARCHIVE_FILE core.txz PLIST core.plist REQUIRED HIDDEN DISABLED DOWNLOADED)
#@@ENDCASE

#@@CASE legacy_cpack_component_invalid_forms
#@@OUTCOME ERROR
include(CPackComponent)
cpack_add_component()
#@@ENDCASE
