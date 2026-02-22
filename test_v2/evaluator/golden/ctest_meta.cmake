project(CTestDemo)
enable_testing()
add_test(NAME smoke COMMAND app)
add_executable(app main.c)