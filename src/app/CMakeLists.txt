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

qt6_add_qml_module( ${AppName} 
    URI ${AppName}
    VERSION 1.0
    QML_FILES main.qml
    RESOURCE_PREFIX "/"
)

add_custom_command(TARGET ${AppName} POST_BUILD COMMAND Qt6::windeployqt --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" "$<TARGET_FILE:${AppName}>" ) 
