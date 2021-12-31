#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "video_capture.h"

#include <dwmapi.h>

#pragma comment (lib, "dwmapi.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define VIDEO_CAPTURE_BUFFER_COUNT 2

typedef struct {
	DWORD Flags;
	DWORD Length;
	DWORD Padding1;
	DWORD Padding2;
	LPCWCHAR Ptr;
} HSTRING;

typedef enum {
	DQTAT_COM_NONE = 0,
	DQTAT_COM_ASTA = 1,
	DQTAT_COM_STA = 2
} DISPATCHERQUEUE_THREAD_APARTMENTTYPE;

typedef enum {
	DQTYPE_THREAD_DEDICATED = 1,
	DQTYPE_THREAD_CURRENT = 2,
} DISPATCHERQUEUE_THREAD_TYPE;

typedef struct {
	DWORD                                dwSize;
	DISPATCHERQUEUE_THREAD_TYPE          threadType;
	DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartmentType;
} DispatcherQueueOptions;

#define IUnknown_Parent(_type) \
	HRESULT(STDMETHODCALLTYPE* QueryInterface)(_type* self, const GUID* riid, void** object); \
	ULONG(STDMETHODCALLTYPE* AddRef)(_type* self); \
	ULONG(STDMETHODCALLTYPE* Release)(_type* self)

#define IInspectable_Parent(_type) \
	IUnknown_Parent(_type); \
	void* GetIids; \
	void* GetRuntimeClassName; \
	void* GetTrustLevel

typedef struct IInspectable                        IInspectable;
typedef struct IClosable                           IClosable;
typedef struct IGraphicsCaptureSession             IGraphicsCaptureSession;
typedef struct ITypedEventHandler                  ITypedEventHandler;
typedef struct IGraphicsCaptureSession2            IGraphicsCaptureSession2;
typedef struct IGraphicsCaptureItemInterop         IGraphicsCaptureItemInterop;
typedef struct IGraphicsCaptureItem                IGraphicsCaptureItem;
typedef struct IDirect3D11CaptureFramePoolStatics  IDirect3D11CaptureFramePoolStatics;
typedef struct IDirect3D11CaptureFramePoolStatics2 IDirect3D11CaptureFramePoolStatics2;
typedef struct IDirect3D11CaptureFramePool         IDirect3D11CaptureFramePool;
typedef struct IDirect3D11CaptureFrame             IDirect3D11CaptureFrame;
typedef struct IDirect3DDevice                     IDirect3DDevice;
typedef struct IDirect3DSurface                    IDirect3DSurface;
typedef struct IDirect3DDxgiInterfaceAccess        IDirect3DDxgiInterfaceAccess;

// don't really care about contents of IDispatcherQueueController
typedef IInspectable IDispatcherQueueController;

struct IInspectableVtbl {
	IInspectable_Parent(IInspectable);
};

struct IClosableVtbl {
	IInspectable_Parent(IClosable);
	HRESULT(STDMETHODCALLTYPE* Close)(IClosable* this);
};

struct ITypedEventHandlerVtbl {
	IUnknown_Parent(ITypedEventHandler);
	HRESULT(STDMETHODCALLTYPE* Invoke)(ITypedEventHandler* this, IInspectable* sender, IInspectable* args);
};

struct IGraphicsCaptureSessionVtbl {
	IInspectable_Parent(IGraphicsCaptureSession);
	HRESULT(STDMETHODCALLTYPE* StartCapture)(IGraphicsCaptureSession* this);
};

struct IGraphicsCaptureSession2Vtbl {
	IInspectable_Parent(IGraphicsCaptureSession2);
	HRESULT(STDMETHODCALLTYPE* get_IsCursorCaptureEnabled)(IGraphicsCaptureSession2* this, char* value);
	HRESULT(STDMETHODCALLTYPE* put_IsCursorCaptureEnabled)(IGraphicsCaptureSession2* this, char value);
};

struct IGraphicsCaptureItemInteropVtbl {
	IUnknown_Parent(IGraphicsCaptureItemInterop);
	HRESULT(STDMETHODCALLTYPE* CreateForWindow)(IGraphicsCaptureItemInterop* this, HWND window, const GUID* riid, void** result);
	HRESULT(STDMETHODCALLTYPE* CreateForMonitor)(IGraphicsCaptureItemInterop* this, HMONITOR monitor, const GUID* riid, void** result);
};

struct IGraphicsCaptureItemVtbl {
	IInspectable_Parent(IGraphicsCaptureItem);
	void* get_DisplayName;
	HRESULT(STDMETHODCALLTYPE* get_Size)(IGraphicsCaptureItem* this, SIZE* size);
	HRESULT(STDMETHODCALLTYPE* add_Closed)(IGraphicsCaptureItem* this, ITypedEventHandler* handler, UINT64* token);
	HRESULT(STDMETHODCALLTYPE* remove_Closed)(IGraphicsCaptureItem* this, UINT64 token);
};

struct IDirect3D11CaptureFramePoolStaticsVtbl {
	IInspectable_Parent(IDirect3D11CaptureFramePoolStatics);
	HRESULT(STDMETHODCALLTYPE* Create)(IDirect3D11CaptureFramePoolStatics* this, IDirect3DDevice* device, DXGI_FORMAT pixelFormat, INT32 numberOfBuffers, SIZE size, IDirect3D11CaptureFramePool** result);
};

struct IDirect3D11CaptureFramePoolStatics2Vtbl {
	IInspectable_Parent(IDirect3D11CaptureFramePoolStatics2);
	HRESULT(STDMETHODCALLTYPE* CreateFreeThreaded)(IDirect3D11CaptureFramePoolStatics2* self, IDirect3DDevice* device, DXGI_FORMAT pixelFormat, INT32 numberOfBuffers, SIZE size, IDirect3D11CaptureFramePool** result);
};


struct IDirect3D11CaptureFramePoolVtbl {
	IInspectable_Parent(IDirect3D11CaptureFramePool);
	HRESULT(STDMETHODCALLTYPE* Recreate)(IDirect3D11CaptureFramePool* this, IDirect3DDevice* device, DXGI_FORMAT pixelFormat, INT32 numberOfBuffers, SIZE size);
	HRESULT(STDMETHODCALLTYPE* TryGetNextFrame)(IDirect3D11CaptureFramePool* this, IDirect3D11CaptureFrame** result);
	HRESULT(STDMETHODCALLTYPE* add_FrameArrived)(IDirect3D11CaptureFramePool* this, ITypedEventHandler* handler, UINT64* token);
	HRESULT(STDMETHODCALLTYPE* remove_FrameArrived)(IDirect3D11CaptureFramePool* this, UINT64 token);
	HRESULT(STDMETHODCALLTYPE* CreateCaptureSession)(IDirect3D11CaptureFramePool* this, IGraphicsCaptureItem* item, IGraphicsCaptureSession** result);
};

struct IDirect3D11CaptureFrameVtbl {
	IInspectable_Parent(IDirect3D11CaptureFrame);
	HRESULT(STDMETHODCALLTYPE* get_Surface)(IDirect3D11CaptureFrame* this, IDirect3DSurface** value);
	HRESULT(STDMETHODCALLTYPE* get_SystemRelativeTime)(IDirect3D11CaptureFrame* this, UINT64* value);
	HRESULT(STDMETHODCALLTYPE* get_ContentSize)(IDirect3D11CaptureFrame* this, SIZE* size);
};

struct IDirect3DDeviceVtbl {
	IInspectable_Parent(IDirect3DDevice);
	void* Trim;
};

struct IDirect3DSurfaceVtbl {
	IInspectable_Parent(IDirect3DSurface);
	void* get_Description;
};

struct IDirect3DDxgiInterfaceAccessVtbl {
	IUnknown_Parent(IDirect3DDxgiInterfaceAccess);
	HRESULT(STDMETHODCALLTYPE* GetInterface)(IDirect3DDxgiInterfaceAccess* this, const GUID* riid, void** object);
};


#define VTBL(name) struct name { const struct name ## Vtbl* vtbl; }
VTBL(IClosable);
VTBL(IInspectable);
VTBL(IGraphicsCaptureSession);
VTBL(IGraphicsCaptureSession2);
VTBL(IGraphicsCaptureItemInterop);
VTBL(IGraphicsCaptureItem);
VTBL(IDirect3D11CaptureFramePoolStatics);
VTBL(IDirect3D11CaptureFramePoolStatics2);
VTBL(IDirect3D11CaptureFramePool);
VTBL(IDirect3D11CaptureFrame);
VTBL(IDirect3DDevice);
VTBL(IDirect3DSurface);
VTBL(IDirect3DDxgiInterfaceAccess);
#undef VTBL

DEFINE_GUID(IID_IClosable, 0x30d5a829, 0x7fa4, 0x4026, 0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e);
DEFINE_GUID(IID_IGraphicsCaptureSession2, 0x2c39ae40, 0x7d2e, 0x5044, 0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e);
DEFINE_GUID(IID_IGraphicsCaptureItemInterop, 0x3628e81b, 0x3cac, 0x4c60, 0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56);
DEFINE_GUID(IID_IGraphicsCaptureItem, 0x79c3f95b, 0x31f7, 0x4ec2, 0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60);
DEFINE_GUID(IID_IGraphicsCaptureItemHandler, 0xe9c610c0, 0xa68c, 0x5bd9, 0x80, 0x21, 0x85, 0x89, 0x34, 0x6e, 0xee, 0xe2);
DEFINE_GUID(IID_IDirect3D11CaptureFramePoolStatics, 0x7784056a, 0x67aa, 0x4d53, 0xae, 0x54, 0x10, 0x88, 0xd5, 0xa8, 0xca, 0x21);
DEFINE_GUID(IID_IDirect3D11CaptureFramePoolStatics2, 0x589b103f, 0x6bbc, 0x5df5, 0xa9, 0x91, 0x02, 0xe2, 0x8b, 0x3b, 0x66, 0xd5);
DEFINE_GUID(IID_IDirect3D11CaptureFramePoolHandler, 0x51a947f7, 0x79cf, 0x5a3e, 0xa3, 0xa5, 0x12, 0x89, 0xcf, 0xa6, 0xdf, 0xe8);
DEFINE_GUID(IID_IDirect3DDxgiInterfaceAccess, 0xa9b3d012, 0x3df2, 0x4ee3, 0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1);

#define STATIC_HSTRING(name, str) static HSTRING name = { 1, sizeof(str) - 1, 0, 0, L## str }
STATIC_HSTRING(GraphicsCaptureSessionName, "Windows.Graphics.Capture.GraphicsCaptureSession");
STATIC_HSTRING(GraphicsCaptureItemName, "Windows.Graphics.Capture.GraphicsCaptureItem");
STATIC_HSTRING(Direct3D11CaptureFramePoolName, "Windows.Graphics.Capture.Direct3D11CaptureFramePool");
#undef STATIC_HSTRING

static RECT VideoCapture__GetRect(VideoCapture* Capture, int Width, int Height)
{
	if (Capture->Window) // capturing only one window
	{
		RECT WindowRect;
		if (FAILED(DwmGetWindowAttribute(Capture->Window, DWMWA_EXTENDED_FRAME_BOUNDS, &WindowRect, sizeof(WindowRect))))
		{
			return (RECT) { 0 };
		}

		if (Capture->OnlyClientArea) // need only client area of window
		{
			RECT ClientRect;
			GetClientRect(Capture->Window, &ClientRect);

			POINT TopLeft = { 0, 0 };
			ClientToScreen(Capture->Window, &TopLeft);

			RECT Rect;
			Rect.left = max(0, TopLeft.x - WindowRect.left);
			Rect.top = max(0, TopLeft.y - WindowRect.top);
			Rect.right = Rect.left + min(ClientRect.right, Width - Rect.left);
			Rect.bottom = Rect.top + min(ClientRect.bottom, Height - Rect.top);
			return Rect;
		}
		else // need whole window size
		{
			return (RECT)
			{
				.left = 0,
				.top = 0,
				.right = WindowRect.right - WindowRect.left,
				.bottom = WindowRect.bottom - WindowRect.top,
			};
		}
	}
	else // capturing whole monitor, or rectangle on it
	{
		return Capture->Rect;
	}
}

static HRESULT STDMETHODCALLTYPE VideoCapture__QueryInterface(ITypedEventHandler* This, const GUID* Riid, void** Object)
{
	if (Object == NULL)
	{
		return E_POINTER;
	}
	if (IsEqualGUID(Riid, &IID_IGraphicsCaptureItemHandler) ||
		IsEqualGUID(Riid, &IID_IDirect3D11CaptureFramePoolHandler) ||
		IsEqualGUID(Riid, &IID_IAgileObject) ||
		IsEqualGUID(Riid, &IID_IUnknown))
	{
		*Object = This;
		return S_OK;
	}
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE VideoCapture__AddRef(ITypedEventHandler* This)
{
	return 1;
}

static ULONG STDMETHODCALLTYPE VideoCapture__Release(ITypedEventHandler* This)
{
	return 1;
}

static HRESULT STDMETHODCALLTYPE VideoCapture__OnClosed(ITypedEventHandler* This, IInspectable* Sender, IInspectable* Args)
{
	VideoCapture* Capture = CONTAINING_RECORD(This, VideoCapture, OnCloseHandler);
	Capture->CloseCallback(Capture);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE VideoCapture__OnFrame(ITypedEventHandler* This, IInspectable* Sender, IInspectable* Args)
{
	VideoCapture* Capture = CONTAINING_RECORD(This, VideoCapture, OnFrameHandler);

	// hmm.. sometimes it succeeds, but returns NULL frame???
	IDirect3D11CaptureFrame* Frame;
	if (SUCCEEDED(Capture->FramePool->vtbl->TryGetNextFrame(Capture->FramePool, &Frame)) && Frame != NULL)
	{
		SIZE Size;
		UINT64 Time;
		HR(Frame->vtbl->get_SystemRelativeTime(Frame, &Time));
		HR(Frame->vtbl->get_ContentSize(Frame, &Size));

		IDirect3DSurface* Surface;
		HR(Frame->vtbl->get_Surface(Frame, &Surface));

		IDirect3DDxgiInterfaceAccess* Access;
		HR(Surface->vtbl->QueryInterface(Surface, &IID_IDirect3DDxgiInterfaceAccess, (LPVOID*)&Access));
		Surface->vtbl->Release(Surface);

		ID3D11Texture2D* Texture;
		HR(Access->vtbl->GetInterface(Access, &IID_ID3D11Texture2D, (LPVOID*)&Texture));
		Access->vtbl->Release(Access);

		D3D11_TEXTURE2D_DESC Desc;
		ID3D11Texture2D_GetDesc(Texture, &Desc);

		RECT Rect = VideoCapture__GetRect(Capture, min(Size.cx, (LONG)Desc.Width), min(Size.cy, (LONG)Desc.Height));
		if (Rect.right > Rect.left && Rect.bottom > Rect.top)
		{
			// call callback only when window has non-zero size, which will happen when it is minimzed
			Capture->FrameCallback(Capture, Texture, &Rect, Time);
		}
		ID3D11Texture2D_Release(Texture);
		Frame->vtbl->Release(Frame);

		if (Capture->CurrentSize.cx != Size.cx || Capture->CurrentSize.cy != Size.cy)
		{
			Capture->CurrentSize = Size;
			HR(Capture->FramePool->vtbl->Recreate(Capture->FramePool, Capture->Device, DXGI_FORMAT_B8G8R8A8_UNORM, VIDEO_CAPTURE_BUFFER_COUNT, Size));
		}
	}

	return S_OK;
}

static const struct ITypedEventHandlerVtbl VideoCapture__CloseHandlerVtbl = {
	&VideoCapture__QueryInterface,
	&VideoCapture__AddRef,
	&VideoCapture__Release,
	&VideoCapture__OnClosed,
};

static const struct ITypedEventHandlerVtbl VideoCapture__FrameHandlerVtbl = {
	&VideoCapture__QueryInterface,
	&VideoCapture__AddRef,
	&VideoCapture__Release,
	&VideoCapture__OnFrame,
};

void VideoCapture_Init(VideoCapture* Capture, VideoCapture_OnCloseCallback* CloseCallback, VideoCapture_OnFrameCallback* FrameCallback)
{
	// load these entry points dynamically, so we can start .exe on older windows for displaying nicer error message
	HMODULE CombaseModule = LoadLibraryW(L"combase.dll");
	//HMODULE CoreMessagingModule = LoadLibraryW(L"CoreMessaging.dll");
	HMODULE Direct3D11Module = GetModuleHandleW(L"d3d11.dll");
	Assert(CombaseModule);
	//Assert(CoreMessagingModule);
	Assert(Direct3D11Module);

	HRESULT(WINAPI * RoInitialize)(DWORD) = (void*)GetProcAddress(CombaseModule, "RoInitialize");
	HRESULT(WINAPI * RoGetActivationFactory)(HSTRING*, const GUID*, void**) = (void*)GetProcAddress(CombaseModule, "RoGetActivationFactory");
	//HRESULT(WINAPI * CreateDispatcherQueueController)(DispatcherQueueOptions, IDispatcherQueueController**) = (void*)GetProcAddress(CoreMessagingModule, "CreateDispatcherQueueController");
	Capture->CreateDirect3D11DeviceFromDXGIDevice = (void*)GetProcAddress(Direct3D11Module, "CreateDirect3D11DeviceFromDXGIDevice");
	Assert(RoInitialize);
	Assert(RoGetActivationFactory);
	//Assert(CreateDispatcherQueueController);
	Assert(Capture->CreateDirect3D11DeviceFromDXGIDevice);

	const DWORD RO_INIT_SINGLETHREADED = 0;
	HR(RoInitialize(RO_INIT_SINGLETHREADED));

	HR(RoGetActivationFactory(&GraphicsCaptureItemName, &IID_IGraphicsCaptureItemInterop, (LPVOID*)&Capture->ItemInterop));
	HR(RoGetActivationFactory(&Direct3D11CaptureFramePoolName, &IID_IDirect3D11CaptureFramePoolStatics2, (LPVOID*)&Capture->FramePoolStatics));

	Capture->OnCloseHandler.vtbl = &VideoCapture__CloseHandlerVtbl;
	Capture->OnFrameHandler.vtbl = &VideoCapture__FrameHandlerVtbl;

	// create dispatcher queue that will call callbacks on main thread as part of message loop
	//DispatcherQueueOptions Options =
	//{
	//	.dwSize = sizeof(Options),
	//	.threadType = DQTYPE_THREAD_CURRENT,
	//	.apartmentType = DQTAT_COM_NONE,
	//};

	//// don't really care about object itself, as long as it is created on main thread
	//IDispatcherQueueController* Controller;
	//HR(CreateDispatcherQueueController(Options, &Controller));

	Capture->CloseCallback = CloseCallback;
	Capture->FrameCallback = FrameCallback;
}

void VideoCapture_Done(VideoCapture* Capture)
{
	//Capture->FramePoolStatics
	//Capture->ItemInterop
	//Direct3D11Module
	//CombaseModule
}

bool VideoCapture_CreateForWindow(VideoCapture* Capture, ID3D11Device* Device, HWND Window, bool OnlyClientArea)
{
	IDXGIDevice* DxgiDevice;
	HR(ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (LPVOID*)&DxgiDevice));
	HR(Capture->CreateDirect3D11DeviceFromDXGIDevice(DxgiDevice, (LPVOID*)&Capture->Device));
	IDXGIDevice_Release(DxgiDevice);

	IGraphicsCaptureItem* Item;
	if (SUCCEEDED(Capture->ItemInterop->vtbl->CreateForWindow(Capture->ItemInterop, Window, &IID_IGraphicsCaptureItem, (LPVOID*)&Item)))
	{
		SIZE Size;
		HR(Item->vtbl->get_Size(Item, &Size));

		IDirect3D11CaptureFramePool* FramePool;
		if (SUCCEEDED(Capture->FramePoolStatics->vtbl->CreateFreeThreaded(Capture->FramePoolStatics, Capture->Device, DXGI_FORMAT_B8G8R8A8_UNORM, VIDEO_CAPTURE_BUFFER_COUNT, Size, &FramePool)))
		{
			Capture->Item = Item;
			Capture->FramePool = FramePool;
			Capture->OnlyClientArea = OnlyClientArea;
			Capture->Window = Window;

			RECT Rect = VideoCapture__GetRect(Capture, Size.cx, Size.cy);
			Capture->CurrentSize.cx = Rect.right - Rect.left;
			Capture->CurrentSize.cy = Rect.bottom - Rect.top;
			Capture->Rect = Rect;

			return true;
		}
		Item->vtbl->Release(Item);
	}

	Capture->Device->vtbl->Release(Capture->Device);
	Capture->Device = NULL;

	return false;
}

bool VideoCapture_CreateForMonitor(VideoCapture* Capture, ID3D11Device* Device, HMONITOR Monitor, const RECT* Rect)
{
	IDXGIDevice* DxgiDevice;
	HR(ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (LPVOID*)&DxgiDevice));
	HR(Capture->CreateDirect3D11DeviceFromDXGIDevice(DxgiDevice, (LPVOID*)&Capture->Device));
	IDXGIDevice_Release(DxgiDevice);

	IGraphicsCaptureItem* Item;
	if (SUCCEEDED(Capture->ItemInterop->vtbl->CreateForMonitor(Capture->ItemInterop, Monitor, &IID_IGraphicsCaptureItem, (LPVOID*)&Item)))
	{
		SIZE Size;
		HR(Item->vtbl->get_Size(Item, &Size));

		IDirect3D11CaptureFramePool* FramePool;
		if (SUCCEEDED(Capture->FramePoolStatics->vtbl->CreateFreeThreaded(Capture->FramePoolStatics, Capture->Device, DXGI_FORMAT_B8G8R8A8_UNORM, 2, Size, &FramePool)))
		{
			Capture->Item = Item;
			Capture->FramePool = FramePool;
			Capture->OnlyClientArea = FALSE;
			Capture->Window = NULL;
			Capture->CurrentSize = Size;
			Capture->Rect = Rect ? *Rect : (RECT) { 0, 0, Size.cx, Size.cy };

			return true;
		}
		Item->vtbl->Release(Item);
	}

	Capture->Device->vtbl->Release(Capture->Device);
	Capture->Device = NULL;

	return false;
}

void VideoCapture_Start(VideoCapture* Capture, bool WithMouseCursor)
{
	IGraphicsCaptureSession* Session;
	HR(Capture->FramePool->vtbl->CreateCaptureSession(Capture->FramePool, Capture->Item, &Session));

	IGraphicsCaptureSession2* Session2;
	if (SUCCEEDED(Session->vtbl->QueryInterface(Session, &IID_IGraphicsCaptureSession2, (LPVOID*)&Session2)))
	{
		Session2->vtbl->put_IsCursorCaptureEnabled(Session2, WithMouseCursor ? 1 : 0);
		Session2->vtbl->Release(Session2);
	}

	HR(Capture->Item->vtbl->add_Closed(Capture->Item, &Capture->OnCloseHandler, &Capture->OnCloseToken));
	HR(Capture->FramePool->vtbl->add_FrameArrived(Capture->FramePool, &Capture->OnFrameHandler, &Capture->OnFrameToken));
	HR(Session->vtbl->StartCapture(Session));

	Capture->Session = Session;
}

void VideoCapture_Stop(VideoCapture* Capture)
{
	if (Capture->Session)
	{
		IClosable* Closable;

		HR(Capture->FramePool->vtbl->remove_FrameArrived(Capture->FramePool, Capture->OnFrameToken));
		HR(Capture->Item->vtbl->remove_Closed(Capture->Item, Capture->OnCloseToken));

		HR(Capture->Session->vtbl->QueryInterface(Capture->Session, &IID_IClosable, (LPVOID*)&Closable));
		Closable->vtbl->Close(Closable);
		Closable->vtbl->Release(Closable);

		HR(Capture->FramePool->vtbl->QueryInterface(Capture->FramePool, &IID_IClosable, (LPVOID*)&Closable));
		Closable->vtbl->Close(Closable);
		Closable->vtbl->Release(Closable);

		Capture->Session->vtbl->Release(Capture->Session);
		Capture->Session = NULL;
	}

	if (Capture->Item)
	{
		Capture->FramePool->vtbl->Release(Capture->FramePool);
		Capture->Item->vtbl->Release(Capture->Item);
		Capture->Item = NULL;
	}

	if (Capture->Device)
	{
		Capture->Device->vtbl->Release(Capture->Device);
		Capture->Device = NULL;
	}
}
