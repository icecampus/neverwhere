#include "main_window.h"
#include <QQuickItem>
#include <QSettings>
#include <QScreen>
#include <QApplication>


#include <windows.h>
#include <windef.h>
#include "Windowsx.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


namespace
{
    const int TITLE_SIZE = 30;
    const int BORDER_SIZE_SYSTEM = ::GetSystemMetrics(SM_CXSIZEFRAME);

    LRESULT bordersHitTest(const QQuickWindow& mainWindow, const QPoint& ptMouse)
    {
        static constexpr int BORDER_MARGIN = 4;

        auto frameRect = mainWindow.geometry();
        auto contentsRect = mainWindow.geometry();

        contentsRect.adjust(BORDER_MARGIN, BORDER_MARGIN, -BORDER_MARGIN, -BORDER_MARGIN);

        USHORT uRow = 1;
        USHORT uCol = 1;
        if (ptMouse.y() >= frameRect.top() && ptMouse.y()  < contentsRect.top())
        {
            uRow = 0;
        }
        else if (ptMouse.y() < frameRect.bottom() && ptMouse.y()  >= contentsRect.bottom())
        {
            uRow = 2;
        }

        if (ptMouse.x()  >= frameRect.left() && ptMouse.x() < contentsRect.left())
        {
            uCol = 0;
        }
        else if (ptMouse.x()  < frameRect.right() && ptMouse.x()  >= contentsRect.right())
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

//EpicEditorWindow
EpicEditorWindow::EpicEditorWindow(QWindow* parent)
	: QQuickWindow(parent)
{
    setFlags(Qt::FramelessWindowHint);
    if (QSysInfo::productType() == "windows")
    {
        HWND hWnd = (HWND)winId();
        DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    }
    if (QSysInfo::productType() == "windows") {
        HWND hwnd = (HWND)winId();
        DWORD attr = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &attr, sizeof(attr));
    }

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
            *result = 0;
            return true;
        }
		break;

	case WM_NCHITTEST:
        POINT winApiMouse = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };

        // Конвертируем в логические (Qt)
        qreal dpiScale = devicePixelRatio();
        QPoint qtMouse = QPoint(
            winApiMouse.x / dpiScale,
            winApiMouse.y / dpiScale
        );

        *result = bordersHitTest(*this, qtMouse);

		if (*result == HTCLIENT) // mouse is inside the content area
		{
			if (captionHitTest(qtMouse))
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
    if (!_caption)
        return false;

    QPointF itemLocalPos = _caption->mapFromGlobal(ptMouse);
    bool isInside = _caption->contains(itemLocalPos);

    return isInside;
}

void EpicEditorWindow::loadSetting()
{
}

QString EpicEditorWindow::screenName()
{
	return screen()->name();
}

//void EpicEditorWindow::setScreenIndex(int i)
//{
//	//setScreen(qApp->screens()[i]);
//}

void EpicEditorWindow::onClosing()
{
}
