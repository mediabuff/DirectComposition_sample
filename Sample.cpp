#include "Precompiled.h"
#include "Window.h"

using namespace Microsoft::WRL;
using namespace D2D1;
using namespace std;

extern "C" IMAGE_DOS_HEADER __ImageBase;

static unsigned const CardRows = 3;
static unsigned const CardColumns = 6;
static float const CardMargin = 15.0f;
static float const CardWidth = 150.0f;
static float const CardHeight = 210.0f;

static float const WindowWidth = CardColumns * (CardWidth + CardMargin) + CardMargin;
static float const WindowHeight = CardRows * (CardHeight + CardMargin) + CardMargin;

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

enum class CardStatus
{
	Hidden,
	Selected,
	Matched
};

struct Card
{
	CardStatus Status = CardStatus::Hidden;
	wchar_t Value = L' ';
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
};

struct SampleWindow : Window<SampleWindow>
{
	// Device independent resources
	float m_dpiX = 0.0f;
	float m_dpiY = 0.0f;
	ComPtr<IDWriteTextFormat> m_textFormat;

	// Contains some device resources
	array<Card, CardRows * CardColumns> m_cards;

	// Device resources
	ComPtr<ID3D11Device> m_device3D;
	ComPtr<IDCompositionDevice> m_device;
	ComPtr<IDCompositionTarget> m_target;

	SampleWindow()
	{
		CreateDesktopWindow();
		ShuffleCards();
		CreateTextFormat();
	}

	void CreateTextFormat()
	{
		ComPtr<IDWriteFactory2> factory;

		HR(DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(factory),
			reinterpret_cast<IUnknown **>(factory.GetAddressOf())
		));

		HR(factory->CreateTextFormat(L"Candara",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			CardHeight / 2.0f,
			L"en",
			m_textFormat.GetAddressOf()));

		HR(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
		HR(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	}

	void ShuffleCards()
	{
		random_device device;
		mt19937 generator(device());
		uniform_int_distribution<short> const distribution(L'A', L'Z');

		array<wchar_t, CardRows * CardColumns> values;

		for (unsigned i = 0; i != CardRows * CardColumns / 2; ++i)
		{
			wchar_t const value = distribution(generator);
			values[i * 2 + 0] = value;
			values[i * 2 + 1] = tolower(value);
		}

		shuffle(begin(values), end(values), generator);

		for (unsigned i = 0; i != CardRows * CardColumns; ++i)
		{
			Card & card = m_cards[i];
			card.Value = values[i];
			card.Status = CardStatus::Hidden;
		}

#ifdef _DEBUG
		for (unsigned row = 0; row != CardRows; ++row)
		{
			for (unsigned column = 0; column != CardColumns; ++column)
			{
				Card &card = m_cards[row * CardColumns + column];
				TRACE(L"%c ", card.Value);
			}

			TRACE(L"\n");
		}

		TRACE(L"\n");
#endif
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

		ComPtr<ID2D1DeviceContext> dc;

		HR(device2D->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			dc.GetAddressOf()));

		D2D1_COLOR_F const color = ColorF(0.0f, 0.0f, 0.0f);

		ComPtr<ID2D1SolidColorBrush> brush;

		HR(dc->CreateSolidColorBrush(color, brush.GetAddressOf()));

		float const width = LogicalToPhysical(CardWidth, m_dpiX);
		float const height = LogicalToPhysical(CardHeight, m_dpiY);

		for (unsigned row = 0; row != CardRows; ++row)
			for (unsigned column = 0; column != CardColumns; ++column)
			{
				Card & card = m_cards[row * CardColumns + column];

				card.OffsetX = LogicalToPhysical(column * (CardWidth + CardMargin) + CardMargin, m_dpiX);
				card.OffsetY = LogicalToPhysical(row * (CardHeight + CardMargin) + CardMargin, m_dpiY);

				ComPtr<IDCompositionVisual2> frontVisual = CreateVisual();
				HR(frontVisual->SetOffsetX(card.OffsetX));
				HR(frontVisual->SetOffsetY(card.OffsetY));

				HR(rootVisual->AddVisual(frontVisual.Get(), false, nullptr));

				ComPtr<IDCompositionVisual2> backVisual = CreateVisual();
				HR(backVisual->SetOffsetX(card.OffsetX));
				HR(backVisual->SetOffsetY(card.OffsetY));

				HR(rootVisual->AddVisual(backVisual.Get(), false, nullptr));

				ComPtr<IDCompositionSurface> frontSurface = CreateSurface(width, height);

				HR(frontVisual->SetContent(frontSurface.Get()));

				DrawCardFront(frontSurface, card.Value, brush);

			}

		HR(m_device->Commit());
	}

	void DrawCardFront(ComPtr<IDCompositionSurface> const & surface,
		wchar_t const value,
		ComPtr<ID2D1SolidColorBrush> brush)
	{
		ComPtr<ID2D1DeviceContext> dc;
		POINT offset = {};

		HR(surface->BeginDraw(nullptr,
			__uuidof(dc),
			reinterpret_cast<void **>(dc.GetAddressOf()),
			&offset));

		dc->SetDpi(m_dpiX, m_dpiY);

		dc->SetTransform(Matrix3x2F::Translation(PhysicalToLogical(offset.x, m_dpiX),
			PhysicalToLogical(offset.y, m_dpiY)));

		dc->Clear(ColorF(1.0f, 1.0f, 1.0f));

		dc->DrawText(&value,
			1,
			m_textFormat.Get(),
			RectF(0.0f, 0.0f, CardWidth, CardHeight),
			brush.Get());

		HR(surface->EndDraw());
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
