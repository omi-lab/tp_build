include(tp_build/cmake/build_a.cmake)

function(tp_parse_submodules)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  # extract top project dir from global settings file
  string(REPLACE "\\" "/" TP_CONFIG_PATH "$ENV{TP_CONFIG}")
  if(NOT IS_ABSOLUTE ${TP_CONFIG_PATH})
    set(TP_CONFIG_PATH "${CMAKE_SOURCE_DIR}/${TP_CONFIG_PATH}")
  endif()
  if(NOT EXISTS "${TP_CONFIG_PATH}")
    message(FATAL_ERROR "Config file was not found. Please set TP_CONFIG environmental variable!")
  endif()
  extract_var_value_pair("${TP_CONFIG_PATH}" "TP_TMP_")

  # extract subprojects from top project
  set(PROJECT_DIR ${TP_TMP_PROJECT_DIR})
  message(STATUS "PROJECT_DIR: ${PROJECT_DIR}")

  # execute_process(COMMAND "C:/Program Files/Git/bin/bash.exe" "${CMAKE_CURRENT_LIST_DIR}/tp_build/cmake/extract_submodules.sh" SUBPROJECTS
  #                 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${PROJECT_DIR}"
  #                 OUTPUT_VARIABLE TP_SUBPROJECTS
  #                 OUTPUT_STRIP_TRAILING_WHITESPACE)

  extract_var_value_pair("${CMAKE_CURRENT_LIST_DIR}/${PROJECT_DIR}/submodules.pri" "TP_TMP_")
  set(TP_SUBPROJECTS "${TP_TMP_SUBPROJECTS}")

  if(TP_SUBPROJECTS)
    set(TP_SUBDIRS "${TP_SUBPROJECTS}")

    # extract dependencies for all subprojects
    foreach(subproject ${TP_SUBPROJECTS})
      set(TP_TMP_SUBDIRS "")
        #message(STATUS "TP_SUBDIRS_TMP:31: ${subproject}")
      # execute_process(COMMAND "C:/Program Files/Git/bin/bash.exe" "${CMAKE_CURRENT_LIST_DIR}/tp_build/cmake/extract_submodules.sh" SUBDIRS
      #                 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${subproject}"
      #                 OUTPUT_VARIABLE TP_SUBDIRS_TMP
      #                 OUTPUT_STRIP_TRAILING_WHITESPACE)

      if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${subproject}/submodules.pri")
        extract_var_value_pair("${CMAKE_CURRENT_LIST_DIR}/${subproject}/submodules.pri" "TP_TMP_")
      endif()

      list(APPEND TP_SUBDIRS "${TP_TMP_SUBDIRS}")
    endforeach()
  endif()

  # execute_process(COMMAND "C:/Program Files/Git/bin/bash.exe" "${CMAKE_CURRENT_LIST_DIR}/tp_build/cmake/extract_submodules.sh" SUBDIRS
  #                 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${PROJECT_DIR}"
  #                 OUTPUT_VARIABLE TP_SUBDIRS_TMP
  #                 OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(TP_SUBDIRS_TMP "")
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${PROJECT_DIR}/submodules.pri")
    extract_var_value_pair("${CMAKE_CURRENT_LIST_DIR}/${PROJECT_DIR}/submodules.pri" "TP_TMP_")
  endif()
  list(APPEND TP_SUBDIRS "${TP_TMP_SUBDIRS}")

  list(REMOVE_DUPLICATES TP_SUBDIRS)

  foreach(subdir ${TP_SUBDIRS})
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${subdir}/CMakeLists.txt")
      message(STATUS "adding target: ${subdir}")
      add_subdirectory(${subdir})
    endif()
  endforeach()

  set(TP_TEST_TARGETS "")
  set(TP_TESTS "")
  foreach(subdir ${TP_SUBDIRS})
    set(TP_TEMPLATE "")

    # execute_process(COMMAND "C:/Program Files/Git/bin/bash.exe" "${CMAKE_CURRENT_LIST_DIR}/tp_build/cmake/extract_vars.sh" TEMPLATE
    #                 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${subdir}"
    #                 OUTPUT_VARIABLE TP_TEMPLATE
    #                 OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${subdir}/vars.pri")
      extract_var_value_pair("${CMAKE_CURRENT_LIST_DIR}/${subdir}/vars.pri" "TP_TMP_")
    endif()
    list(APPEND TP_TEST_TARGETS ${TP_TMP_TEMPLATE})
  endforeach()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tests.txt" "${TP_TESTS}")

  configure_file("${CMAKE_CURRENT_LIST_DIR}/tp_build/tp_test/run_tests.sh"
                 "${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh"
                 COPYONLY)

  add_custom_target(tests
                    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh"
                    DEPENDS "${TP_TEST_TARGETS}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

