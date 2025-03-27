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


	QQuickItem* _caption = nullptr;
	bool _loadMaximazed = false;
	bool _maximized = false;
};

