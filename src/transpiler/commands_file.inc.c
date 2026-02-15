static const Command_Spec g_commands_file[] = {
    CMD("file", eval_file_command),
    CMD("configure_file", eval_configure_file_command),
    CMD("write_file", eval_write_file_command),
    CMD("remove", eval_remove_command),
    CMD("make_directory", eval_make_directory_command),
    CMD("write_basic_package_version_file", eval_write_basic_package_version_file_command),
    CMD("configure_package_config_file", eval_configure_package_config_file_command),
};
