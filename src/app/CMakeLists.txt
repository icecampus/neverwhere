set(AppName App)

qt6_add_executable( ${AppName}  
    main.cpp
    qml/qml.qrc
    )

set_target_properties(${AppName} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)


target_link_libraries( ${AppName}  PRIVATE
    Qt6::Core
    Qt6::Quick
)

qt6_add_qml_module( ${AppName} 
    URI QmlDemo
    VERSION 1.0
    QML_FILES qml/main.qml
)

add_custom_command(TARGET ${AppName} POST_BUILD COMMAND Qt6::windeployqt --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:${AppName}>" ) 
