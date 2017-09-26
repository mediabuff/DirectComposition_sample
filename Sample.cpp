#include "Precompiled.h"
#include "Window.h"

using namespace Microsoft::WRL;
using namespace D2D1;

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
	// Device independent resources
	float m_dpiX = 0.0f;
	float m_dpiY = 0.0f;

	// Device resources
	ComPtr<ID3D11Device> m_device3D;
	ComPtr<IDCompositionDevice> m_device;
	ComPtr<IDCompositionTarget> m_target;
	ComPtr<IDCompositionVisual2> m_pointerVisual;
	ComPtr<IDCompositionVisual2> m_backgroundVisual;

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

		RegisterClass(&wc);

		ASSERT(!m_window);

		VERIFY(CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP,
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

	bool IsDeviceCreated() const
	{
		return m_device3D;
	}

	void ReleaseDeviceResources()
	{
		m_device3D.Reset();
	}

	void CreateDevice3D()
	{
		ASSERT(!IsDeviceCreated());

		unsigned flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
			D3D11_CREATE_DEVICE_SINGLETHREADED;
		#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
		#endif

		HR(D3D11CreateDevice(nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			flags,
			nullptr, 0,
			D3D11_SDK_VERSION,
			m_device3D.GetAddressOf(),
			nullptr,
			nullptr));
	}

	ComPtr<ID2D1Device> CreateDevice2D()
	{
		ComPtr<IDXGIDevice3> deviceX;
		HR(m_device3D.As(&deviceX));

		D2D1_CREATION_PROPERTIES properties = {};
#ifdef _DEBUG
		properties.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

		ComPtr<ID2D1Device> device2D;

		HR(D2D1CreateDevice(deviceX.Get(),
			properties,
			device2D.GetAddressOf()));

		return device2D;
	}

	ComPtr<IDCompositionVisual2> CreateVisual()
	{
		ComPtr<IDCompositionVisual2> visual;

		//HR(m_device->CreateVisual(reinterpret_cast<IDCompositionVisual**>(visual.GetAddressOf())));
		HR(m_device->CreateVisual((IDCompositionVisual**)visual.GetAddressOf()));

		return visual;
	}

	template <typename T>
	ComPtr<IDCompositionSurface> CreateSurface(T const width,
		T const height)
	{
		ComPtr<IDCompositionSurface> surface;

		HR(m_device->CreateSurface(
			static_cast<unsigned>(width),
			static_cast<unsigned>(height),
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_ALPHA_MODE_PREMULTIPLIED,
			surface.GetAddressOf()));

		return surface;
	}

	void CreateDeviceResources()
	{
		ASSERT(!IsDeviceCreated());

		CreateDevice3D();

		ComPtr<ID2D1Device> const device2D = CreateDevice2D();

		HR(DCompositionCreateDevice2(device2D.Get(),
			__uuidof(m_device),
			reinterpret_cast<void **>(m_device.ReleaseAndGetAddressOf())));

		HR(m_device->CreateTargetForHwnd(m_window,
			true,
			m_target.ReleaseAndGetAddressOf()));

		m_pointerVisual = CreateVisual();
		m_backgroundVisual = CreateVisual();

		HR(m_target->SetRoot(m_pointerVisual.Get()));

		CreateDeviceScaleResources();

		HR(m_device->Commit());
	}

	void CreateDeviceScaleResources()
	{
		ComPtr<IDCompositionSurface> surface = CreateSurface(
			LogicalToPhysical(100, m_dpiX),
			LogicalToPhysical(100, m_dpiY));

		HR(m_pointerVisual->SetContent(surface.Get()));

		ComPtr<ID2D1DeviceContext> dc;
		POINT offset = {};

		HR(surface->BeginDraw(nullptr,
			__uuidof(dc),
			reinterpret_cast<void **>(dc.GetAddressOf()),
			&offset));

		dc->SetDpi(m_dpiX, m_dpiY);

		dc->SetTransform(Matrix3x2F::Translation(
			PhysicalToLogical(offset.x, m_dpiX),
			PhysicalToLogical(offset.y, m_dpiY)));

		dc->Clear();

		ComPtr<ID2D1SolidColorBrush> brush;
		D2D1_COLOR_F const color = ColorF(1.0f, 0.5f, 0.0f);

		HR(dc->CreateSolidColorBrush(color,
			brush.GetAddressOf()));

		D2D1_ELLIPSE ellipse = Ellipse(Point2F(50.0f, 50.0f),
			50.0f,
			50.0f);

		dc->FillEllipse(ellipse, brush.Get());

		HR(surface->EndDraw());
	}

	LRESULT MessageHandler(UINT const message,
		WPARAM const wparam,
		LPARAM const lparam)
	{
		if (WM_MOUSEMOVE == message)
		{
			MouseMoveHandler(lparam);
		}
		else if (WM_PAINT == message)
		{
			PaintHandler();
		}
		else if (WM_DPICHANGED == message)
		{
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

	void MouseMoveHandler(LPARAM const lparam)
	{
		try
		{
			if (!IsDeviceCreated())
				return;

			float x = LOWORD(lparam);
			float y = HIWORD(lparam);

			x -= LogicalToPhysical(50, m_dpiX);
			y -= LogicalToPhysical(50, m_dpiY);

			HR(m_pointerVisual->SetOffsetX(x));
			HR(m_pointerVisual->SetOffsetY(y));

			HR(m_device->Commit());
		}
		catch (ComException const &e)
		{
			TRACE(L"MouseMoveHandler failed 0x%X\n", e.result);

			ReleaseDeviceResources();

			VERIFY(InvalidateRect(m_window,
				nullptr,
				false));
		}
	}

	void DpiChangedHandler(WPARAM const wparam, LPARAM const lparam)
	{
		try
		{
			m_dpiX = LOWORD(wparam);
			m_dpiY = HIWORD(wparam);

			RECT const * suggested = reinterpret_cast<RECT const*>(lparam);

			VERIFY(SetWindowPos(m_window,
				nullptr,
				suggested->left,
				suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOACTIVATE | SWP_NOZORDER));

			if (!IsDeviceCreated())
				return;

			CreateDeviceScaleResources();

			HR(m_device->Commit());
		}
		catch (ComException const &e)
		{
			TRACE(L"DpiChangedHandler failed 0x%X\n", e.result);

			ReleaseDeviceResources();

			VERIFY(InvalidateRect(m_window, nullptr, false));
		}
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

		RECT rect =
		{
			0,
			0,
			static_cast<LONG>(LogicalToPhysical(WindowWidth, m_dpiX)),
			static_cast<LONG>(LogicalToPhysical(WindowHeight, m_dpiY))
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
	}

	void PaintHandler()
	{
		try
		{
			if (IsDeviceCreated())
			{
				HR(m_device3D->GetDeviceRemovedReason());

			}
			else
			{
				CreateDeviceResources();
			}

			VERIFY(ValidateRect(m_window, nullptr));
		}
		catch (ComException const & e)
		{
			TRACE(L"PaintHandler failed 0x%X\n", e.result);

			ReleaseDeviceResources();
		}
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
