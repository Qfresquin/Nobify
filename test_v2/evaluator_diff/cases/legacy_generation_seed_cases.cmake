#@@CASE legacy_generation_make_directory_aux_source_and_test_driver_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/asd_src/a.c
int a = 0;
#@@END_FILE_TEXT
#@@FILE_TEXT source/asd_src/b.cpp
int b = 0;
#@@END_FILE_TEXT
#@@FILE_TEXT source/asd_src/skip.txt
x
#@@END_FILE_TEXT
#@@FILE_TEXT source/extra.h
#pragma once
#@@END_FILE_TEXT
#@@QUERY VAR ASD_OUT
#@@QUERY VAR LEGACY_DIR_EXISTS
#@@QUERY VAR DRIVER_EXISTS
#@@QUERY VAR DRIVER_HAS_ALPHA
#@@QUERY VAR DRIVER_HAS_BETA
#@@QUERY VAR DRIVER_HAS_BEFORE
#@@QUERY VAR DRIVER_HAS_AFTER
make_directory("${CMAKE_CURRENT_BINARY_DIR}/legacy_dir/sub")
set(CMAKE_TESTDRIVER_BEFORE_TESTMAIN "/*before*/")
set(CMAKE_TESTDRIVER_AFTER_TESTMAIN "/*after*/")
aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/asd_src" ASD_OUT)
create_test_sourcelist(TEST_SRCS generated_driver.c alpha_test.c beta_test.c EXTRA_INCLUDE extra.h FUNCTION setup_hook)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/generated_driver.c")
  set(DRIVER_EXISTS 1)
  file(READ "${CMAKE_CURRENT_BINARY_DIR}/generated_driver.c" DRIVER_TEXT)
else()
  set(DRIVER_EXISTS 0)
  set(DRIVER_TEXT "")
endif()
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/legacy_dir/sub")
  set(LEGACY_DIR_EXISTS 1)
else()
  set(LEGACY_DIR_EXISTS 0)
endif()
string(FIND "${DRIVER_TEXT}" "alpha_test" DRIVER_ALPHA_POS)
string(FIND "${DRIVER_TEXT}" "beta_test" DRIVER_BETA_POS)
string(FIND "${DRIVER_TEXT}" "/*before*/" DRIVER_BEFORE_POS)
string(FIND "${DRIVER_TEXT}" "/*after*/" DRIVER_AFTER_POS)
if(DRIVER_ALPHA_POS EQUAL -1)
  set(DRIVER_HAS_ALPHA 0)
else()
  set(DRIVER_HAS_ALPHA 1)
endif()
if(DRIVER_BETA_POS EQUAL -1)
  set(DRIVER_HAS_BETA 0)
else()
  set(DRIVER_HAS_BETA 1)
endif()
if(DRIVER_BEFORE_POS EQUAL -1)
  set(DRIVER_HAS_BEFORE 0)
else()
  set(DRIVER_HAS_BEFORE 1)
endif()
if(DRIVER_AFTER_POS EQUAL -1)
  set(DRIVER_HAS_AFTER 0)
else()
  set(DRIVER_HAS_AFTER 1)
endif()
#@@ENDCASE

#@@CASE legacy_generation_invalid_make_directory_form_is_rejected
#@@OUTCOME ERROR
make_directory()
#@@ENDCASE
