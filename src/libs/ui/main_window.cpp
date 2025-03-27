#include "main_window.h"
#include <QQuickItem>
#include <QSettings>
#include <QScreen>
#include <QApplication>

#define NOMINMAX
#include <windows.h>
#include <windef.h>
#include "Windowsx.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


namespace
{
    const int BORDER_MARGIN = 8; // Увеличьте отступ для лучшего захвата

    LRESULT bordersHitTest(const QQuickWindow& mainWindow, const QPoint& ptMouse) 
    {
        const QRect rect = mainWindow.geometry();

        // Определение зон захвата
        const int resizeBorder = BORDER_MARGIN;

        bool left = ptMouse.x() <= rect.left() + resizeBorder;
        bool right = ptMouse.x() >= rect.right() - resizeBorder;
        bool top = ptMouse.y() <= rect.top() + resizeBorder;
        bool bottom = ptMouse.y() >= rect.bottom() - resizeBorder;

        if (left && top) return HTTOPLEFT;
        if (left && bottom) return HTBOTTOMLEFT;
        if (right && top) return HTTOPRIGHT;
        if (right && bottom) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        return HTCLIENT;
    }
}

//EpicEditorWindow
EpicEditorWindow::EpicEditorWindow(QWindow* parent)
	: QQuickWindow(parent)
{
    if (QSysInfo::productType() == "windows") 
    {
        HWND hwnd = (HWND)winId();

        // Включаем стандартную тень
        MARGINS margins = {-1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        // Добавляем стандартный оконный стиль
        SetWindowLongPtr(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION);
    }

	setMinimumHeight(600);
	setMinimumWidth(800);

	connect(this, SIGNAL(closing(QQuickCloseEvent*)), this, SLOT(onClosing()));
}

//
bool EpicEditorWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
	const auto msg = (MSG*)message;

	switch (msg->message)
	{
        case WM_SIZE:
        {
	        if (const auto maximized = msg->wParam == SIZE_MAXIMIZED; maximized != m_maximized)
	        {
		        m_maximized = maximized;
		        HWND hWnd = (HWND)winId();
		        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
                return true;
	        }
	        break;
        }
        case WM_NCCALCSIZE:
        {
            if (msg->wParam == TRUE)
            {
                // Для Windows 10/11 оставляем стандартные рамки
                *result = 0;
                return true;
            }
            break;
        }
        case WM_NCHITTEST:
        {
            POINT winApiMouse = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
            qreal dpiScale = devicePixelRatio();
            QPoint qtMouse = QPoint(
                winApiMouse.x / dpiScale,
                winApiMouse.y / dpiScale
            );

            LRESULT hit = bordersHitTest(*this, qtMouse);

            if (hit == HTCLIENT) 
            {
                // Проверка на область заголовка
                if (captionHitTest(qtMouse)) 
                {
                    hit = HTCAPTION;
                }
            }

            *result = hit;
            return true; // Перехватываем сообщение
        }
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);
            HWND hwnd = msg->hwnd;

            // Получаем информацию о мониторе
            HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo;
            monitorInfo.cbSize = sizeof(MONITORINFO);
            GetMonitorInfo(hMonitor, &monitorInfo);

            // Рабочая область (без панели задач)
            RECT workArea = monitorInfo.rcWork;

            // Преобразование абсолютных координат в относительные для текущего монитора
            mmi->ptMaxPosition.x = workArea.left - monitorInfo.rcMonitor.left;
            mmi->ptMaxPosition.y = workArea.top - monitorInfo.rcMonitor.top;

            // Максимальный размер окна = размер рабочей области
            mmi->ptMaxSize.x = workArea.right - workArea.left;
            mmi->ptMaxSize.y = workArea.bottom - workArea.top;

            // Минимальный размер окна
            const QSize minSize = minimumSize();
            mmi->ptMinTrackSize.x = minSize.width();
            mmi->ptMinTrackSize.y = minSize.height();

            // Учет пользовательских ограничений максимального размера
            const QSize maxSize = maximumSize();
            if (maxSize.isValid()) {
                mmi->ptMaxTrackSize.x = std::min(maxSize.width(), static_cast<int>(mmi->ptMaxSize.x));
                mmi->ptMaxTrackSize.y = std::min(maxSize.height(), static_cast<int>(mmi->ptMaxSize.y));
            }
            else {
                mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
                mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;
            }

            *result = 0;
            return true;
        }
    }

    return QQuickWindow::nativeEvent(eventType, message, result);
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
