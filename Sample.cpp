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
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
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

		ComPtr<IDCompositionVisual2> rootVisual = CreateVisual();

		HR(m_target->SetRoot(rootVisual.Get()));

		HR(m_device->Commit());
	}

	LRESULT MessageHandler(UINT const message,
		WPARAM const wparam,
		LPARAM const lparam)
	{
		if (WM_PAINT == message)
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
		else if (WM_WINDOWPOSCHANGING == message)
		{
			// Prevent window resizing due to device lost
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

		RECT const * suggested = reinterpret_cast<RECT const*>(lparam);

		D2D1_SIZE_U const size = GetEffectiveWindowSize();

		VERIFY(SetWindowPos(m_window,
			nullptr,
			suggested->left,
			suggested->top,
			size.width,
			size.height,
			SWP_NOACTIVATE | SWP_NOZORDER));

		ReleaseDeviceResources();
	}

	D2D1_SIZE_U GetEffectiveWindowSize()
	{
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

		return SizeU(rect.right - rect.left,
			rect.bottom - rect.top);
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

		D2D1_SIZE_U const size = GetEffectiveWindowSize();

		VERIFY(SetWindowPos(m_window,
			nullptr,
			0, 0,
			size.width,
			size.height,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER));
	}

	void PaintHandler()
	{
		try
		{
			RECT rect = {};
			VERIFY(GetClientRect(m_window, &rect));

			TRACE(L"Paint %d %d (%.2f %.2f)\n",
				rect.right,
				rect.bottom,
				PhysicalToLogical(rect.right, m_dpiX),
				PhysicalToLogical(rect.bottom, m_dpiY));

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
