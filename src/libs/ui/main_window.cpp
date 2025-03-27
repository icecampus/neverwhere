#include "main_window.h"
#include <windows.h>
#include <windef.h>
#include <QQuickItem>
#include <QSettings>
#include <QScreen>
#include <QApplication>
#include "Windowsx.h"



namespace
{
    const int TITLE_SIZE = 30;
    const int BORDER_SIZE_SYSTEM = ::GetSystemMetrics(SM_CXSIZEFRAME);

    LRESULT bordersHitTest(const QQuickWindow& mainWindow, const POINT& ptMouse)
    {
        static constexpr int BORDER_MARGIN = 4;

        auto frameRect = mainWindow.geometry();
        auto contentsRect = mainWindow.geometry();

        contentsRect.adjust(BORDER_MARGIN, BORDER_MARGIN, -BORDER_MARGIN, -BORDER_MARGIN);

        USHORT uRow = 1;
        USHORT uCol = 1;
        if (ptMouse.y >= frameRect.top() && ptMouse.y < contentsRect.top())
        {
            uRow = 0;
        }
        else if (ptMouse.y < frameRect.bottom() && ptMouse.y >= contentsRect.bottom())
        {
            uRow = 2;
        }

        if (ptMouse.x >= frameRect.left() && ptMouse.x < contentsRect.left())
        {
            uCol = 0;
        }
        else if (ptMouse.x < frameRect.right() && ptMouse.x >= contentsRect.right())
        {
            uCol = 2;
        }

        LRESULT hitTests[3][3] = {
            {HTTOPLEFT, HTTOP, HTTOPRIGHT},
            {HTLEFT, HTCLIENT, HTRIGHT},
            {HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT},
        };

        return hitTests[uRow][uCol];
    }
}

//
EpicEditorWindow::EpicEditorWindow(QWindow* parent)
	: QQuickWindow(parent)
{
	setMinimumHeight(600);
	setMinimumWidth(800);

	connect(this, SIGNAL(closing(QQuickCloseEvent*)), this, SLOT(onClosing()));
}

//
bool EpicEditorWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
	
	const auto msg = (MSG*)message;
	bool catchEvent = false;

	switch (msg->message)
	{
	case WM_SIZE:
		if (const auto maximized = msg->wParam == SIZE_MAXIMIZED; maximized != m_maximized)
		{
			m_maximized = maximized;
			HWND hWnd = (HWND)winId();
			SetWindowPos(
				hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
		}
		break;

	case WM_NCCALCSIZE:
		if (msg->wParam == TRUE)
		{
			if (m_maximized) // add border in maximized state
			{
				qDebug() << "WM_NCCALCSIZE:" << m_maximized;
				auto* pncsp = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
				pncsp->rgrc[0].left = pncsp->rgrc[0].left + 2 * BORDER_SIZE_SYSTEM;
				pncsp->rgrc[0].top = pncsp->rgrc[0].top + 2 * BORDER_SIZE_SYSTEM;
				pncsp->rgrc[0].right = pncsp->rgrc[0].right - 2 * BORDER_SIZE_SYSTEM;
				pncsp->rgrc[0].bottom = pncsp->rgrc[0].bottom - 2 * BORDER_SIZE_SYSTEM;
			}

			*result = FALSE;
			catchEvent = true;
		}
		else
		{
			catchEvent = false;
		}
		break;

	case WM_NCHITTEST:
		const POINT mouse = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
		*result = bordersHitTest(*this, mouse);

		if (*result == HTCLIENT) // mouse is inside the content area
		{
			if (captionHitTest(QPoint(mouse.x, mouse.y)))
			{
				*result = HTCAPTION;
			}
		}
		catchEvent = true;
		break;
	}

	return catchEvent;
}

bool EpicEditorWindow::captionHitTest(const QPoint& ptMouse)
{
	if (_caption)
	{
  //      QPoint captionPos = mapToGlobal(QPoint(_caption->x(), _caption->y()));
  //      QRect captionRect(captionPos.x(), captionPos.y(), _caption->width(), _caption->height());

  //      qDebug() << "=========";
  //      qDebug() << "global mouse x: " << ptMouse.x() << ", y: " << ptMouse.y();
  //      qDebug() << "caption pos  x: " << captionPos.x() << ", y: " << captionPos.y();

		//return captionRect.contains(ptMouse);

		QPointF localPos  = mapFromGlobal(QPointF(ptMouse));

        qDebug() << "=========";
		qDebug() << "global mouse x: " << ptMouse.x() << ", y: " << ptMouse.y();
		qDebug() << "caption pos  x: " << localPos.x() << ", y: " << localPos.y();

        return  _caption->contains(localPos);
	}

	return false;
}

void EpicEditorWindow::loadSetting()
{
	//QSettings settings("screen.conf", QSettings::IniFormat);

	//int windowX = settings.value("x", 200).toInt();
	//int windowY = settings.value("y", 200).toInt();
	//int windowWidth = settings.value("width", 1024).toInt();
	//int windowHeight = settings.value("height", 768).toInt();

	//m_loadMaximazed = settings.value("maximized", false).toBool();
	//int screenIndex = settings.value("screenIndex", 0).toInt();

	//// qDebug() << "x: " << windowX;
	//// qDebug() << "y: " << windowY;

	//// qDebug() << "width: " << windowWidth;
	//// qDebug() << "height: " << windowHeight;

	//// qDebug() << "maximized: " << m_loadMaximazed;
	//// qDebug() << "screenIndex: " << screenIndex;

	//if (m_loadMaximazed)
	//{
	//	setGeometry(windowX, 200, 1024, 768);
	//}
	//else
	//{
	//	setGeometry(windowX, windowY, windowWidth, windowHeight);
	//}

	//setScreenIndex(screenIndex);
}

QString EpicEditorWindow::screenName()
{
	return screen()->name();
}

void EpicEditorWindow::setScreenIndex(int i)
{
	//setScreen(qApp->screens()[i]);
}

void EpicEditorWindow::onClosing()
{
	//// emit saveParams();
	//QSettings settings("screen.conf", QSettings::IniFormat);

	//int windowX = x();
	//int windowY = y();

	//int windowWidth = width();
	//int windowHeight = height();

	//int screenIndex = qApp->screens().indexOf(screen());

	//// qDebug() << "x: " << windowX;
	//// qDebug() << "y: " << windowY;
	////
	//// qDebug() << "width: "	<< windowWidth;
	//// qDebug() << "height: "	<< windowHeight;

	//// qDebug() << "maximized: " << m_maximized;
	//// qDebug() << "screenIndex: " << screenIndex;

	//settings.setValue("x", windowX);
	//settings.setValue("y", windowY);

	//settings.setValue("width", windowWidth);
	//settings.setValue("height", windowHeight);

	//settings.setValue("maximized", m_maximized);
	//settings.setValue("screenIndex", screenIndex);
}
