#pragma once
#include <QQuickWindow>
#include <QQuickItem>

class EpicEditorWindow : public QQuickWindow
{
	Q_OBJECT

	Q_PROPERTY(QQuickItem* caption READ getCaption WRITE setCaption NOTIFY captionChanged)

public:
	EpicEditorWindow(QWindow* parent = nullptr);

	
	bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

	QQuickItem* getCaption() { return _caption; }
	void setCaption(QQuickItem* caption)
	{
		_caption = caption;
		emit captionChanged();
	}

	Q_INVOKABLE QString screenName();
	Q_INVOKABLE void loadSetting();
	Q_INVOKABLE bool isNeedMaximized() { return _loadMaximazed; }

public slots:
	void onClosing();

signals:
	void captionChanged();
	void saveParams();

private:
	void initBlurBehind();
	bool captionHitTest(const QPoint& ptMouse);
	// Force QWindow::geometry to the real OS client size (logical px). Qt's
	// own win32 geometry update misses some paths with the custom frame
	// (creation clamp, external moves, monitor/DPI hops), and a stale
	// geometry lays the QML out at the declared 1920x1080 with the right
	// side off-screen. Guarded by a size check, so no resize loop.
	void syncGeometryFromOs();


	QQuickItem* _caption = nullptr;
	bool _loadMaximazed = false;
	bool _maximized = false;
};

