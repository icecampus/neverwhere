set(AppName App)

qt6_add_executable( ${AppName}  
    main.cpp
)

set_target_properties(${AppName} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries( ${AppName}  PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
)

qt_generate_deploy_qml_app_script(
    TARGET ${AppName}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)

qt6_add_qml_module( ${AppName} 
    URI ${AppName}
    VERSION 1.0
    QML_FILES qml/main.qml
    RESOURCE_PREFIX "/"
)




add_custom_command(TARGET ${AppName} POST_BUILD COMMAND Qt6::windeployqt --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" "$<TARGET_FILE:${AppName}>" ) 
