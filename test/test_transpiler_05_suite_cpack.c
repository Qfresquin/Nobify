#include "test_transpiler_shared.h"

TEST(cpack_component_commands_materialize_manifest_and_variables) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_readme.txt", "pkg");
    const char *input =
        "project(Test)\n"
        "cpack_add_install_type(Full DISPLAY_NAME \"Full Install\")\n"
        "cpack_add_component_group(Runtime DISPLAY_NAME \"Runtime Group\" DESCRIPTION \"Runtime files\" EXPANDED)\n"
        "cpack_add_component(core DISPLAY_NAME \"Core\")\n"
        "cpack_add_component(runtime DISPLAY_NAME \"Runtime\" DESCRIPTION \"Runtime component\" GROUP Runtime REQUIRED DEPENDS core INSTALL_TYPES Full)\n"
        "install(FILES temp_pkg_readme.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"ALL_${CPACK_COMPONENTS_ALL}\" \"GRP_${CPACK_COMPONENT_RUNTIME_GROUP}\" \"REQ_${CPACK_COMPONENT_RUNTIME_REQUIRED}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cpack_components_manifest.txt") != NULL);
    ASSERT(strstr(output, "component:runtime|display=Runtime|description=Runtime component|group=Runtime|required=ON") != NULL);
    ASSERT(strstr(output, "group:Runtime|display=Runtime Group|description=Runtime files") != NULL);
    ASSERT(strstr(output, "install_type:Full|display=Full Install") != NULL);
    ASSERT(strstr(output, "-DGRP_Runtime") != NULL);
    ASSERT(strstr(output, "-DREQ_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component_group") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_install_type") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_readme.txt");
    TEST_PASS();
}

TEST(fase1_cleanup_getters_regression_targets_install_tests_and_cpack) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_library(core STATIC core.c)\n"
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app)\n"
        "set_tests_properties(smoke PROPERTIES TIMEOUT 45)\n"
        "get_test_property(smoke TIMEOUT TMO)\n"
        "get_property(TS GLOBAL PROPERTY TARGETS)\n"
        "string(REPLACE \";\" \"_\" TS_FLAT \"${TS}\")\n"
        "install(TARGETS app core RUNTIME DESTINATION bin ARCHIVE DESTINATION ar)\n"
        "cpack_add_install_type(Full DISPLAY_NAME \"Full Install\")\n"
        "cpack_add_component_group(Runtime DISPLAY_NAME \"Runtime Group\")\n"
        "cpack_add_component(core_comp GROUP Runtime INSTALL_TYPES Full)\n"
        "add_executable(aux aux.c)\n"
        "target_compile_definitions(aux PRIVATE \"TMO_${TMO}\" \"TS_${TS_FLAT}\" \"IT_${CPACK_ALL_INSTALL_TYPES}\" \"CG_${CPACK_COMPONENT_GROUPS_ALL}\" \"CC_${CPACK_COMPONENTS_ALL}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DTMO_45") != NULL);
    ASSERT(strstr(output, "-DIT_Full") != NULL);
    ASSERT(strstr(output, "-DCG_Runtime") != NULL);
    ASSERT(strstr(output, "-DCC_core_comp") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"bin\")") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"ar\")") != NULL);
    ASSERT(strstr(output, "-DTS_app_core_aux") != NULL || strstr(output, "-DTS_app_core") != NULL || strstr(output, "-DTS_core_app") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("set_tests_properties") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_test_property") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_install_type") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component_group") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cpack_archive_module_normalizes_metadata_and_generates_archive_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_archive.txt", "archive");
    const char *input =
        "project(Demo VERSION 2.5.1)\n"
        "include(CPackArchive)\n"
        "set(CPACK_PACKAGE_NAME MyPkg)\n"
        "set(CPACK_GENERATOR TGZ;ZIP)\n"
        "cpack_add_component(base)\n"
        "cpack_add_component(core DEPENDS base)\n"
        "install(FILES temp_pkg_archive.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"N_${CPACK_PACKAGE_NAME}\" "
        "\"V_${CPACK_PACKAGE_VERSION}\" "
        "\"C_${CPACK_COMPONENTS_ALL}\" "
        "\"D_${CPACK_PACKAGE_DEPENDS}\" "
        "\"AE_${CPACK_ARCHIVE_ENABLED}\" "
        "\"AX_${CPACK_ARCHIVE_FILE_EXTENSION}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DN_MyPkg") != NULL);
    ASSERT(strstr(output, "-DV_2.5.1") != NULL);
    ASSERT(strstr(output, "-DC_base;core") != NULL);
    ASSERT(strstr(output, "-DD_base") != NULL);
    ASSERT(strstr(output, "-DAE_ON") != NULL);
    ASSERT(strstr(output, "-DAX_.tar.gz") != NULL);
    ASSERT(strstr(output, "cpack_archive_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_archive.txt");
    TEST_PASS();
}

TEST(cpack_archive_defaults_from_project_when_unset) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Awesome VERSION 1.4.0)\n"
        "include(CPackArchive)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"N_${CPACK_PACKAGE_NAME}\" "
        "\"V_${CPACK_PACKAGE_VERSION}\" "
        "\"AE_${CPACK_ARCHIVE_ENABLED}\" "
        "\"G_${CPACK_ARCHIVE_GENERATORS}\" "
        "\"F_${CPACK_PACKAGE_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DN_Awesome") != NULL);
    ASSERT(strstr(output, "-DV_1.4.0") != NULL);
    ASSERT(strstr(output, "-DAE_ON") != NULL);
    ASSERT(strstr(output, "-DG_TGZ") != NULL);
    ASSERT(strstr(output, "-DF_Awesome-1.4.0") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cpack_deb_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_deb.txt", "deb");
    const char *input =
        "project(MyProj VERSION 3.2.1)\n"
        "include(CPackDeb)\n"
        "set(CPACK_PACKAGE_NAME My Suite)\n"
        "set(CPACK_PACKAGE_DEPENDS libssl;zlib1g)\n"
        "cpack_add_component(core)\n"
        "cpack_add_component(runtime DEPENDS core)\n"
        "install(FILES temp_pkg_deb.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_DEB_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_DEB_ENABLED}\" "
        "\"N_${CPACK_DEBIAN_PACKAGE_NAME}\" "
        "\"V_${CPACK_DEBIAN_PACKAGE_VERSION}\" "
        "\"A_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}\" "
        "\"D_${CPACK_DEBIAN_PACKAGE_DEPENDS}\" "
        "\"F_${CPACK_DEBIAN_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_my-suite") != NULL);
    ASSERT(strstr(output, "-DV_3.2.1") != NULL);
    ASSERT(strstr(output, "-DA_amd64") != NULL);
    ASSERT(strstr(output, "libssl") != NULL);
    ASSERT(strstr(output, "zlib1g") != NULL);
    ASSERT(strstr(output, ".deb") != NULL);
    ASSERT(strstr(output, "cpack_deb_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_deb.txt");
    TEST_PASS();
}

TEST(cpack_rpm_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_rpm.txt", "rpm");
    const char *input =
        "project(MyProj VERSION 4.1.0)\n"
        "include(CPackRPM)\n"
        "set(CPACK_PACKAGE_NAME Server App)\n"
        "set(CPACK_PACKAGE_DEPENDS openssl;libcurl)\n"
        "set(CPACK_RPM_PACKAGE_LICENSE MIT)\n"
        "install(FILES temp_pkg_rpm.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_RPM_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_RPM_ENABLED}\" "
        "\"N_${CPACK_RPM_PACKAGE_NAME}\" "
        "\"V_${CPACK_RPM_PACKAGE_VERSION}\" "
        "\"A_${CPACK_RPM_PACKAGE_ARCHITECTURE}\" "
        "\"L_${CPACK_RPM_PACKAGE_LICENSE}\" "
        "\"R_${CPACK_RPM_PACKAGE_REQUIRES}\" "
        "\"F_${CPACK_RPM_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_server-app") != NULL);
    ASSERT(strstr(output, "-DV_4.1.0") != NULL);
    ASSERT(strstr(output, "-DA_x86_64") != NULL);
    ASSERT(strstr(output, "-DL_MIT") != NULL);
    ASSERT(strstr(output, "openssl") != NULL);
    ASSERT(strstr(output, "libcurl") != NULL);
    ASSERT(strstr(output, ".rpm") != NULL);
    ASSERT(strstr(output, "cpack_rpm_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_rpm.txt");
    TEST_PASS();
}

TEST(cpack_nsis_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_nsis.txt", "nsis");
    const char *input =
        "project(MyProj VERSION 5.0.2)\n"
        "include(CPackNSIS)\n"
        "set(CPACK_PACKAGE_NAME Fancy App)\n"
        "set(CPACK_PACKAGE_CONTACT dev@company.test)\n"
        "set(CPACK_NSIS_DISPLAY_NAME FancyApp-5.0.2)\n"
        "set(CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY FancyApp)\n"
        "install(FILES temp_pkg_nsis.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_NSIS_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_NSIS_ENABLED}\" "
        "\"D_${CPACK_NSIS_DISPLAY_NAME}\" "
        "\"I_${CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY}\" "
        "\"C_${CPACK_NSIS_CONTACT}\" "
        "\"F_${CPACK_NSIS_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DD_FancyApp-5.0.2") != NULL);
    ASSERT(strstr(output, "-DI_FancyApp") != NULL);
    ASSERT(strstr(output, "dev@company.test") != NULL);
    ASSERT(strstr(output, ".exe") != NULL);
    ASSERT(strstr(output, "cpack_nsis_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_nsis.txt");
    TEST_PASS();
}

TEST(cpack_wix_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_wix.txt", "wix");
    const char *input =
        "project(MyProj VERSION 6.1.3)\n"
        "include(CPackWIX)\n"
        "set(CPACK_PACKAGE_NAME Tool Suite)\n"
        "set(CPACK_WIX_PRODUCT_NAME ToolSuite)\n"
        "set(CPACK_WIX_CULTURES pt-br;en-us)\n"
        "install(FILES temp_pkg_wix.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_WIX_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_WIX_ENABLED}\" "
        "\"N_${CPACK_WIX_PRODUCT_NAME}\" "
        "\"A_${CPACK_WIX_ARCHITECTURE}\" "
        "\"C_${CPACK_WIX_CULTURES}\" "
        "\"F_${CPACK_WIX_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_ToolSuite") != NULL);
    ASSERT(strstr(output, "-DA_x64") != NULL);
    ASSERT(strstr(output, "-DC_pt-br;en-us") != NULL);
    ASSERT(strstr(output, ".msi") != NULL);
    ASSERT(strstr(output, "cpack_wix_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_wix.txt");
    TEST_PASS();
}

TEST(cpack_dmg_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_dmg.txt", "dmg");
    const char *input =
        "project(MyProj VERSION 7.2.0)\n"
        "include(CPackDMG)\n"
        "set(CPACK_PACKAGE_NAME Mac Suite)\n"
        "set(CPACK_DMG_VOLUME_NAME MacSuite)\n"
        "set(CPACK_DMG_FORMAT UDZO)\n"
        "install(FILES temp_pkg_dmg.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_DMG_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_DMG_ENABLED}\" "
        "\"V_${CPACK_DMG_VOLUME_NAME}\" "
        "\"F_${CPACK_DMG_FORMAT}\" "
        "\"O_${CPACK_DMG_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DV_MacSuite") != NULL);
    ASSERT(strstr(output, "-DF_UDZO") != NULL);
    ASSERT(strstr(output, ".dmg") != NULL);
    ASSERT(strstr(output, "cpack_dmg_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_dmg.txt");
    TEST_PASS();
}

TEST(cpack_bundle_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_bundle.txt", "bundle");
    const char *input =
        "project(MyProj VERSION 7.3.1)\n"
        "include(CPackBundle)\n"
        "set(CPACK_BUNDLE_NAME AppBundle)\n"
        "set(CPACK_BUNDLE_ICON AppIcon.icns)\n"
        "install(FILES temp_pkg_bundle.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_BUNDLE_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_BUNDLE_ENABLED}\" "
        "\"N_${CPACK_BUNDLE_NAME}\" "
        "\"I_${CPACK_BUNDLE_ICON}\" "
        "\"F_${CPACK_BUNDLE_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_AppBundle") != NULL);
    ASSERT(strstr(output, "-DI_AppIcon.icns") != NULL);
    ASSERT(strstr(output, ".app") != NULL);
    ASSERT(strstr(output, "cpack_bundle_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_bundle.txt");
    TEST_PASS();
}

TEST(cpack_productbuild_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_productbuild.txt", "pkg");
    const char *input =
        "project(MyProj VERSION 8.0.0)\n"
        "include(CPackProductBuild)\n"
        "set(CPACK_PACKAGE_NAME Mac Installer)\n"
        "set(CPACK_PRODUCTBUILD_IDENTIFIER com.example.macinstaller)\n"
        "set(CPACK_PRODUCTBUILD_IDENTITY_NAME DeveloperID)\n"
        "install(FILES temp_pkg_productbuild.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_PRODUCTBUILD_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_PRODUCTBUILD_ENABLED}\" "
        "\"I_${CPACK_PRODUCTBUILD_IDENTIFIER}\" "
        "\"S_${CPACK_PRODUCTBUILD_IDENTITY_NAME}\" "
        "\"F_${CPACK_PRODUCTBUILD_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DI_com.example.macinstaller") != NULL);
    ASSERT(strstr(output, "-DS_DeveloperID") != NULL);
    ASSERT(strstr(output, ".pkg") != NULL);
    ASSERT(strstr(output, "cpack_productbuild_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_productbuild.txt");
    TEST_PASS();
}

TEST(cpack_ifw_module_and_configure_file_generate_manifest_and_output) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_ifw_template.txt.in", "Name=@CPACK_IFW_PACKAGE_NAME@\nTitle=@CPACK_IFW_PACKAGE_TITLE@\n");
    write_test_file("temp_pkg_ifw.txt", "ifw");
    const char *input =
        "project(MyProj VERSION 9.0.0)\n"
        "include(CPackIFW)\n"
        "include(CPackIFWConfigureFile)\n"
        "set(CPACK_IFW_PACKAGE_NAME MyIFW)\n"
        "set(CPACK_IFW_PACKAGE_TITLE MyInstaller)\n"
        "cpack_ifw_configure_file(temp_ifw_template.txt.in temp_ifw_generated.txt @ONLY)\n"
        "install(FILES temp_pkg_ifw.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_IFW_MODULE_INITIALIZED}\" "
        "\"C_${CMAKE_CPACK_IFW_CONFIGURE_FILE_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_IFW_ENABLED}\" "
        "\"N_${CPACK_IFW_PACKAGE_NAME}\" "
        "\"T_${CPACK_IFW_PACKAGE_TITLE}\" "
        "\"F_${CPACK_IFW_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DC_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_MyIFW") != NULL);
    ASSERT(strstr(output, "-DT_MyInstaller") != NULL);
    ASSERT(strstr(output, ".ifw") != NULL);
    ASSERT(strstr(output, "cpack_ifw_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_ifw_configure_file") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_ifw_template.txt.in");
    nob_delete_file("temp_ifw_generated.txt");
    nob_delete_file("temp_pkg_ifw.txt");
    TEST_PASS();
}

TEST(cpack_nuget_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_nuget.txt", "nupkg");
    const char *input =
        "project(MyProj VERSION 1.2.3)\n"
        "include(CPackNuGet)\n"
        "set(CPACK_NUGET_PACKAGE_ID My.Package)\n"
        "set(CPACK_NUGET_PACKAGE_AUTHORS Team)\n"
        "set(CPACK_NUGET_PACKAGE_DESCRIPTION CorePackage)\n"
        "install(FILES temp_pkg_nuget.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_NUGET_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_NUGET_ENABLED}\" "
        "\"I_${CPACK_NUGET_PACKAGE_ID}\" "
        "\"A_${CPACK_NUGET_PACKAGE_AUTHORS}\" "
        "\"D_${CPACK_NUGET_PACKAGE_DESCRIPTION}\" "
        "\"F_${CPACK_NUGET_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DI_My.Package") != NULL);
    ASSERT(strstr(output, "-DA_Team") != NULL);
    ASSERT(strstr(output, "-DD_CorePackage") != NULL);
    ASSERT(strstr(output, ".nupkg") != NULL);
    ASSERT(strstr(output, "cpack_nuget_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_nuget.txt");
    TEST_PASS();
}

TEST(cpack_freebsd_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_freebsd.txt", "freebsd");
    const char *input =
        "project(MyProj VERSION 2.0.0)\n"
        "include(CPackFreeBSD)\n"
        "set(CPACK_FREEBSD_PACKAGE_NAME mytool)\n"
        "set(CPACK_FREEBSD_PACKAGE_ORIGIN devel/mytool)\n"
        "install(FILES temp_pkg_freebsd.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_FREEBSD_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_FREEBSD_ENABLED}\" "
        "\"N_${CPACK_FREEBSD_PACKAGE_NAME}\" "
        "\"O_${CPACK_FREEBSD_PACKAGE_ORIGIN}\" "
        "\"F_${CPACK_FREEBSD_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_mytool") != NULL);
    ASSERT(strstr(output, "-DO_devel/mytool") != NULL);
    ASSERT(strstr(output, ".pkg.txz") != NULL);
    ASSERT(strstr(output, "cpack_freebsd_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_freebsd.txt");
    TEST_PASS();
}

TEST(cpack_cygwin_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_cygwin.txt", "cygwin");
    const char *input =
        "project(MyProj VERSION 3.1.4)\n"
        "include(CPackCygwin)\n"
        "set(CPACK_CYGWIN_PACKAGE_NAME mycyg)\n"
        "set(CPACK_CYGWIN_PACKAGE_DEPENDS libstdc++;zlib)\n"
        "install(FILES temp_pkg_cygwin.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_CYGWIN_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_CYGWIN_ENABLED}\" "
        "\"N_${CPACK_CYGWIN_PACKAGE_NAME}\" "
        "\"D_${CPACK_CYGWIN_PACKAGE_DEPENDS}\" "
        "\"F_${CPACK_CYGWIN_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_mycyg") != NULL);
    ASSERT(strstr(output, "libstdc++") != NULL);
    ASSERT(strstr(output, ".tar.xz") != NULL);
    ASSERT(strstr(output, "cpack_cygwin_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_cygwin.txt");
    TEST_PASS();
}

void run_transpiler_suite_cpack(int *passed, int *failed) {
    test_cpack_component_commands_materialize_manifest_and_variables(passed, failed);
    test_fase1_cleanup_getters_regression_targets_install_tests_and_cpack(passed, failed);
    test_cpack_archive_module_normalizes_metadata_and_generates_archive_manifest(passed, failed);
    test_cpack_archive_defaults_from_project_when_unset(passed, failed);
    test_cpack_deb_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_rpm_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_nsis_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_wix_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_dmg_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_bundle_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_productbuild_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_ifw_module_and_configure_file_generate_manifest_and_output(passed, failed);
    test_cpack_nuget_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_freebsd_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_cygwin_module_normalizes_metadata_and_generates_manifest(passed, failed);
}
