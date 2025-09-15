include(CheckCXXSymbolExists)
# check_preprocessor
function(check_preprocessor output_variable symbol)
    set(CMAKE_REQUIRED_QUIET OFF)
    check_cxx_symbol_exists(${symbol} "" ${output_variable})
    if(NOT ${output_variable})
        set(${output_variable} 0 CACHE INTERNAL "Have symbol ${symbol}" FORCE)
    endif()
endfunction(check_preprocessor)

#check_preprocessor_configuration
function(check_preprocessor_configuration)
    check_preprocessor(USE_LINUX __linux__)
    check_preprocessor(USE_WINDOWS _WIN32)
    check_preprocessor(USE_APPLE __APPLE__)
    check_preprocessor(USE_ANDROID __ANDROID__)
    check_preprocessor(USE_FREEBSD __FreeBSD__)
endfunction()


# Print text with color, colors can be one of 
# normal black red green yellow blue magenta cyan white
function(print_rich_text text color)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env CLICOLOR_FORCE=1 ${CMAKE_COMMAND} -E cmake_echo_color --${color} "${text}")
endfunction(print_rich_text)

# Truncate text to length add three commas to the end
function(text_truncate text length return)
    string(LENGTH "${text}" text_length)
    if(${text_length} GREATER ${length})
        math(EXPR text_length "${length} - 3")
        string(SUBSTRING "${text}" 0 ${text_length} text)
        string(APPEND text "...")
    endif()
    set(${return} "${text}" PARENT_SCOPE)
endfunction(text_truncate)

# Pads text from left and right with given symbol to given target_length
function(text_pad_lr text pad_symbol total_length return)
    string(LENGTH "${text}" text_length)
    math(EXPR pad_length "(${total_length} - ${text_length}) / 2")
    if(${pad_length} GREATER 0)
        string(REPEAT "${pad_symbol}" ${pad_length} pad_text)
        set(text "${pad_text}${text}")
    endif()
    
    string(LENGTH "${text}" text_length)
    math(EXPR pad_length "${total_length} - ${text_length}")
    if(${pad_length} GREATER 0)
        string(REPEAT "${pad_symbol}" ${pad_length} pad_text)
        set(text "${text}${pad_text}")
    endif()

    set(${return} "${text}" PARENT_SCOPE)
endfunction(text_pad_lr)

# Print header
function(print_header header_text)
    string(TIMESTAMP header_time "%H:%M:%S")
    set(output_text "[${header_time}] ${header_text}")
    text_truncate("${output_text}" 70 output_text)
    text_pad_lr(" ${output_text} " "=" 80 output_text)
    print_rich_text("${output_text}" "magenta")
endfunction(print_header)

# Print info
function(print_info info_text)
    print_rich_text("${info_text}" "yellow")
endfunction(print_info)


# projects helpers
# getProjectName
function(getProjectName projetNameOutput)
    get_filename_component(projectName ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    set(${projetNameOutput}  ${projectName} PARENT_SCOPE)
endfunction()


# accumulate_files
function(accumulate_files base_project_dir source_dir result_name extensions)
    file(GLOB res LIST_DIRECTORIES True "${source_dir}/*")
    list(LENGTH res res_lengh)
    foreach(dir_iter ${res})
        if(IS_DIRECTORY ${dir_iter})
        accumulate_files("${base_project_dir}" "${dir_iter}" files_in_sub_dir "${extensions}")
        endif()
    endforeach()

    set(directory_files "")
    foreach(current_extension ${extensions})
        file(GLOB found_files LIST_DIRECTORIES True "${source_dir}/*.${current_extension}")
        list(APPEND directory_files ${found_files})
    endforeach()

    # append files to project
    file(RELATIVE_PATH realative_path ${base_project_dir} ${source_dir})
    source_group("${realative_path}" FILES ${directory_files})

    # return value
    list(APPEND directory_files "${files_in_sub_dir}")
    set(${result_name} ${directory_files} PARENT_SCOPE)
endfunction()

function(accumulate_files_relative base_project_dir path_for_relative source_dir result_name extensions)
    accumulate_files("${base_project_dir}" "${source_dir}" files_in_sub_dir "${extensions}")

    set(relative_files "")
    foreach(file IN ITEMS ${files_in_sub_dir})
        file(RELATIVE_PATH relative_path
                "${path_for_relative}"
                "${file}"
        )
        set(relative_files ${relative_files} ${relative_path})
    endforeach()

    set(${result_name} ${relative_files} PARENT_SCOPE)
endfunction()


function(collect_sources resultVarName)
    accumulate_files("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" console_tool_source_h_files h)
    accumulate_files("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" console_tool_source_cpp_files cpp)
    accumulate_files("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" console_tool_source_mm_files mm)
    accumulate_files("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" md_files MD)

    set(all_sources)

    # Добавляем элементы из каждой переменной
    list(APPEND sources ${console_tool_source_h_files})
    list(APPEND sources ${console_tool_source_cpp_files})
    list(APPEND sources ${console_tool_source_mm_files})

    set(${resultVarName} ${sources} PARENT_SCOPE)
endfunction()

# LIBS
# nw_add_lib_sources
function(nw_add_lib_sources)

    set(options "")
    set(oneValueArgs NAME GROUP)
    set(multiValueArgs SOURCES LIBS)
    cmake_parse_arguments(ARG
                        "${options}" 
                        "${oneValueArgs}"
                        "${multiValueArgs}" 
                        ${ARGN} )

    # message(FATAL_ERROR "GROUP: ${ARG_GROUP}")
    # message("ADD_PROJECT_SOURCES: ${ADD_PROJECT_SOURCES}")
    # message("ADD_PROJECT_LIBS: ${ADD_PROJECT_LIBS}")

    # check required arg
    if(NOT ARG_NAME)
        message(FATAL_ERROR "NAME is required!")
    endif()



    add_library(${ARG_NAME} ${ARG_SOURCES})

    if(USE_WINDOWS)
        target_compile_options(${ARG_NAME} PRIVATE "/MP")
    endif()

    target_include_directories(${ARG_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_precompile_headers(${ARG_NAME} PRIVATE pch.h)

    target_link_libraries(${ARG_NAME} PUBLIC ${ARG_LIBS})

    # grouping in source tree
    set_property(TARGET ${ARG_NAME} PROPERTY FOLDER ${ARG_GROUP}) 
endfunction()

# nw_add_lib
function(nw_add_lib)

    set(options "")
    set(oneValueArgs GROUP)
    set(multiValueArgs LIBS)
    cmake_parse_arguments(ARG
                        "${options}" 
                        "${oneValueArgs}"
                        "${multiValueArgs}" 
                        ${ARGN} )


    collect_sources(sources)
    getProjectName(libName)

    nw_add_lib_sources(${TargetGroup} 
        NAME 
            ${libName}
        GROUP 
            ${ARG_GROUP}
        SOURCES 
            ${sources} 
        LIBS 
            ${ARG_LIBS}
    )

    set(LibName ${LibName} PARENT_SCOPE)

endfunction()

# APPS
# nw_add_app_sources
function(nw_add_app_sources)

    set(options "")
    set(oneValueArgs NAME GROUP)
    set(multiValueArgs SOURCES LIBS)
    cmake_parse_arguments(ARG
                        "${options}" 
                        "${oneValueArgs}"
                        "${multiValueArgs}" 
                        ${ARGN} )

    # message(FATAL_ERROR "LIBS: ${ARG_LIBS}")
    # message("ADD_PROJECT_SOURCES: ${ADD_PROJECT_SOURCES}")
    # message("ADD_PROJECT_LIBS: ${ADD_PROJECT_LIBS}")

    # check required arg
    if(NOT ARG_NAME)
        message(FATAL_ERROR "NAME is required!")
    endif()


    #create executable
    # set_target_properties(${AppName} PROPERTIES
    #     WIN32_EXECUTABLE TRUE
    #     MACOSX_BUNDLE TRUE
    # )

    qt_add_resources(ARG_SOURCES resources.qrc)
    
    if(USE_APPLE)
        qt6_add_executable(${ARG_NAME} ${ARG_SOURCES})

        set_target_properties(${ARG_NAME} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.nw.${ARG_NAME}"
            MACOSX_BUNDLE_BUNDLE_NAME ${ARG_NAME}
            XCODE_GENERATE_SCHEME TRUE XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/_intermediate_64"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        )
    elseif(USE_WINDOWS)
        set(RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
        qt6_add_executable(${ARG_NAME} ${ARG_SOURCES} ${RC_FILE})
        # target_compile_options(${AppName} PRIVATE "/MP")
    else()
        qt6_add_executable(${ARG_NAME} ${ARG_SOURCES} )
    endif()

    #add qml
    accumulate_files_relative("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"  "${CMAKE_CURRENT_SOURCE_DIR}/qml" source_qml_files qml)
    qt6_add_qml_module( ${ARG_NAME} 
        URI ${ARG_NAME}
        VERSION 1.0
        QML_FILES ${source_qml_files}
        RESOURCE_PREFIX "/"
    )

    # add_custom_command(TARGET ${ARG_NAME} POST_BUILD COMMAND Qt6::windeployqt --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:${ARG_NAME}>" ) 

    target_include_directories(${ARG_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(${ARG_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/libs")
    target_precompile_headers(${ARG_NAME} PRIVATE pch.h)

    target_link_libraries(${ARG_NAME} PUBLIC ${ARG_LIBS})

    # grouping in source tree
    set_property(TARGET ${ARG_NAME} PROPERTY FOLDER ${ARG_GROUP}) 



    # return AppName
    set(AppName ${ARG_NAME} PARENT_SCOPE)
endfunction()

# nw_add_app
function(nw_add_qml_app)

    set(options "")
    set(oneValueArgs GROUP)
    set(multiValueArgs LIBS)
    cmake_parse_arguments(ARG
                        "${options}" 
                        "${oneValueArgs}"
                        "${multiValueArgs}" 
                        ${ARGN} )


    collect_sources(sources)
    getProjectName(appName)

    nw_add_app_sources(${TargetGroup} 
        NAME 
            ${appName}
        GROUP 
            ${ARG_GROUP}
        SOURCES 
            ${sources} 
        LIBS 
            ${ARG_LIBS}
    )

    set(AppName ${AppName} PARENT_SCOPE)
endfunction()
