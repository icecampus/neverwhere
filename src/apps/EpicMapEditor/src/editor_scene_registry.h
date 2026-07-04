#pragma once

#include <QObject>

class MapModel;
class DiamondIsometryView;
class Chapter;
class AssetToolsSelector;

// Bridges QML-created scene objects to the C++ side (RPC server, automation).
//
// MapView.qml creates MapModel, DiamondIsometryView and AssetToolsSelector
// inside its own QML context; CoreContext has no direct access. When a tab
// is activated, Workspace calls setActiveScene(...) so the RPC server can
// reach the currently open map / iso view / tools / chapter for click /
// save / reload / select_asset / select_tool.
//
// Implementation is inline (header-only) to avoid a recurring MSBuild issue
// where a freshly added .cpp under src/ is compiled into a valid .obj but
// never wired into the link command on the first configure cycle. The
// methods are trivial enough that inlining costs nothing.
class EditorSceneRegistry : public QObject
{
    Q_OBJECT
public:
    explicit EditorSceneRegistry(QObject* parent = nullptr)
        : QObject(parent)
    {}

    // Called from QML (Workspace.qml) after a chapter is loaded.
    Q_INVOKABLE void setActiveScene(MapModel* mapModel,
                                    DiamondIsometryView* isoView,
                                    AssetToolsSelector* tools,
                                    Chapter* chapter)
    {
        m_mapModel = mapModel;
        m_isoView = isoView;
        m_tools = tools;
        m_chapter = chapter;
    }

    // Accessors for the RPC server. May return nullptr if no scene active.
    MapModel* activeMapModel() const { return m_mapModel; }
    DiamondIsometryView* activeIsometryView() const { return m_isoView; }
    AssetToolsSelector* activeTools() const { return m_tools; }
    Chapter* activeChapter() const { return m_chapter; }

private:
    // Raw pointers — these QML objects outlive the registry (parented to
    // the QML engine). The registry is notified of changes via setActiveScene
    // and a new chapter always replaces the old one in the same flow.
    MapModel* m_mapModel = nullptr;
    DiamondIsometryView* m_isoView = nullptr;
    AssetToolsSelector* m_tools = nullptr;
    Chapter* m_chapter = nullptr;
};
