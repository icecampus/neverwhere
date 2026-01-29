nw_add_qml_app(
    GROUP 
        _apps 
    LIBS 
        core
        ui
        generators
        Qt6::Core 
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core5Compat
        Qt6::QuickControls2
    )

accumulate_files_relative(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} png_resources png)
# message(FATAL_ERROR "png_resources: ${png_resources}")

qt6_add_shaders(${AppName} "${AppName}_shaders"
    BATCHABLE
    PRECOMPILE
    OPTIMIZED
    PREFIX
        "/scenegraph/custommaterial"
    FILES
        "shaders/mandelbrot.vert"
        "shaders/mandelbrot.frag"
)

if(APPLE AND CMAKE_GENERATOR MATCHES "Xcode")
    # Отключаем встроенную подпись Xcode (чтобы не конфликтовала)
    set_target_properties(EpicMapEditor PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    )

    # Добавляем пост-сборочную подпись через кастомную команду
    add_custom_command(TARGET EpicMapEditor POST_BUILD
        COMMAND codesign -f -s - "$<TARGET_FILE:EpicMapEditor>"
        COMMENT "Ad-hoc signing ${TARGET_FILE_NAME} for local debugging"
        VERBATIM
    )
endif()