#include "Precompiled.h"
#include "Window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

static float const WindowWidth = 600.0f;
static float const WindowHeight = 400.0f;

struct ComException
{
	HRESULT result;

	ComException(HRESULT const value) : result(value)
	{}
};

static void HR(HRESULT const result)
{
	if (S_OK != result)
	{
		throw ComException(result);
	}
}

template <typename T>
static float PhysicalToLogical(T const pixel,
	float const dpi)
{
	return pixel * 96.0f / dpi;
}

template <typename T>
static float LogicalToPhysical(T const pixel,
	float const dpi)
{
	return pixel * dpi / 96.0f;
}

struct SampleWindow : Window<SampleWindow>
{
	float m_dpiX = 0.0f;
	float m_dpiY = 0.0f;

	SampleWindow()
	{
		CreateDesktopWindow();
	}

	void CreateDesktopWindow()
	{
		WNDCLASS wc = {};
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
		wc.lpszClassName = L"SampleWindow";
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));

		RegisterClass(&wc);

		ASSERT(!m_window);

		VERIFY(CreateWindowEx(0,
			wc.lpszClassName,
			L"Sample Window",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT,
			nullptr,
			nullptr,
			wc.hInstance,
			this
		));

		ASSERT(m_window);
	}

	LRESULT MessageHandler(UINT const message,
		WPARAM const wparam,
		LPARAM const lparam)
	{
		if (WM_ERASEBKGND == message)
		{
			EraseBackgroundHandler(wparam);
			return 1;
		}
		else if (WM_PAINT == message)
		{
			PaintHandler();
		}
		else if (WM_DPICHANGED == message)
		{
			TRACE(L"WM_DPICHANGED\n");
			DpiChangedHandler(wparam, lparam);
		}
		else if (WM_CREATE == message)
		{
			CreateHandler();
		}
		else
		{
			return __super::MessageHandler(message, wparam, lparam);
		}

		return 0;
	}

	void DpiChangedHandler(WPARAM const wparam, LPARAM const lparam)
	{
		m_dpiX = LOWORD(wparam);
		m_dpiY = HIWORD(wparam);

		TRACE(L"DPI %.2f %.2f\n", m_dpiX, m_dpiY);

		RECT const * suggested = reinterpret_cast<RECT const*>(lparam);

		VERIFY(SetWindowPos(m_window,
			nullptr,
			suggested->left,
			suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOACTIVATE | SWP_NOZORDER));
	}

	void CreateHandler()
	{
		HMONITOR const monitor = MonitorFromWindow(m_window,
			MONITOR_DEFAULTTONEAREST);

		unsigned dpiX = 0;
		unsigned dpiY = 0;

		HR(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY));

		m_dpiX = static_cast<float>(dpiX);
		m_dpiY = static_cast<float>(dpiY);

		TRACE(L"DPI %.2f %.2f\n", m_dpiX, m_dpiY);

		RECT rect =
		{
			0,
			0,
			static_cast<unsigned>(LogicalToPhysical(WindowWidth, m_dpiX)),
			static_cast<unsigned>(LogicalToPhysical(WindowHeight, m_dpiY))
		};

		VERIFY(AdjustWindowRect(&rect,
			GetWindowLong(m_window, GWL_STYLE),
			false));

		VERIFY(SetWindowPos(m_window,
			nullptr,
			0, 0,
			rect.right - rect.left,
			rect.bottom - rect.top,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER));

		TRACE(L"Adjusted %d %d %d %d\n",
			rect.left,
			rect.top,
			rect.right - rect.left,
			rect.bottom - rect.top);
	}
	void PaintHandler()
	{
		RECT rect = {};
		VERIFY(GetClientRect(m_window, &rect));

		TRACE(L"Client size: %.2f %.2f\n",
			PhysicalToLogical(rect.right, m_dpiX),
			PhysicalToLogical(rect.bottom, m_dpiY));

		VERIFY(ValidateRect(m_window, nullptr));
	}

	void EraseBackgroundHandler(WPARAM const wparam)
	{
		RECT rect = {};
		VERIFY(GetClientRect(m_window, &rect));

		HBRUSH const brush = static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
		VERIFY(FillRect(reinterpret_cast<HDC>(wparam), &rect, brush));
	}
};

int __stdcall wWinMain(HINSTANCE, 
                       HINSTANCE, 
                       PWSTR, 
                       int)
{
	SampleWindow window;
	MSG message;

	while (GetMessage(&message, nullptr, 0, 0))
	{
		DispatchMessage(&message);
	}
}
