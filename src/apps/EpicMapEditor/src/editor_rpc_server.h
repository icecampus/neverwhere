#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class CoreContext;
class EditorSceneRegistry;

// Raw TCP + line-delimited JSON RPC server for editor automation.
//
// Listens on 127.0.0.1:9877 by default (9876 is taken by the blender MCP
// SSE endpoint, see .mcp.json). Each request is one line of JSON
// terminated by '\n'; each response is one JSON line terminated by '\n'.
// Commands run on the GUI thread (QTcpServer is created there), so they
// may safely touch QObject/QML state directly.
//
// Stateful: the server holds the "current" chapter / asset / tool, set
// via load_chapter / select_asset / select_tool. click(x, y) dispatches
// to AssetToolsSelector::click on the active scene.
class EditorRpcServer : public QObject
{
    Q_OBJECT
public:
    EditorRpcServer(CoreContext* core, EditorSceneRegistry* registry, QObject* parent = nullptr);

    // Returns false (and logs a warning) if the port is already in use.
    bool start(const QHostAddress& addr = QHostAddress::LocalHost, quint16 port = 9877);

signals:
    // Emitted by the load_chapter command; MainWindow.qml opens a Workspace tab.
    void loadChapterRequested(const QString& name, const QString& uuid);

private slots:
    void _onNewConnection();
    void _onReadyRead();
    void _onClientDisconnected();

private:
    void _processLine(QTcpSocket* client, const QByteArray& line);
    QByteArray _dispatch(const QJsonObject& req);
    // Helpers
    static QByteArray _ok(const QJsonObject& data);
    static QByteArray _error(const QString& kind, const QString& message);
    // Command handlers
    QByteArray _cmdPing(const QJsonObject& args);
    QByteArray _cmdStatus(const QJsonObject& args);
    QByteArray _cmdListChapters(const QJsonObject& args);
    QByteArray _cmdLoadChapter(const QJsonObject& args);
    QByteArray _cmdListAssets(const QJsonObject& args);
    QByteArray _cmdSelectAsset(const QJsonObject& args);
    QByteArray _cmdSelectTool(const QJsonObject& args);
    QByteArray _cmdClick(const QJsonObject& args);
    QByteArray _cmdSave(const QJsonObject& args);
    QByteArray _cmdReload(const QJsonObject& args);

    CoreContext* m_core;
    EditorSceneRegistry* m_registry;
    QTcpServer* m_server = nullptr;
    // Partial-line buffering per client.
    QMap<QTcpSocket*, QByteArray> m_buffers;
    // Last-selected tool index (for status reporting).
    int m_currentToolIndex = 0;
};
