#include "Precompiled.h"
#include "Window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

struct SampleWindow : Window<SampleWindow>
{
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
			TRACE(L"WM_ERASEBKGND\n");
			EraseBackgroundHandler(wparam);
			return 1;
		}
		else if (WM_PAINT == message)
		{
			TRACE(L"WM_PAINT\n");
			PaintHandler();
		}
		else
		{
			return __super::MessageHandler(message, wparam, lparam);
		}

		return 0;
	}

	void PaintHandler()
	{
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
