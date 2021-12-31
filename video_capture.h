#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct IGraphicsCaptureItemInterop         IGraphicsCaptureItemInterop;
typedef struct IDirect3D11CaptureFramePoolStatics  IDirect3D11CaptureFramePoolStatics;
typedef struct IDirect3D11CaptureFramePoolStatics2 IDirect3D11CaptureFramePoolStatics2;
typedef struct IDirect3DDevice                     IDirect3DDevice;
typedef struct IGraphicsCaptureItem                IGraphicsCaptureItem;
typedef struct IDirect3D11CaptureFramePool         IDirect3D11CaptureFramePool;
typedef struct IGraphicsCaptureSession             IGraphicsCaptureSession;

typedef struct ITypedEventHandler { const struct ITypedEventHandlerVtbl* vtbl; } ITypedEventHandler;

typedef struct VideoCapture VideoCapture;
typedef void VideoCapture_OnCloseCallback(VideoCapture* Capture);
typedef void VideoCapture_OnFrameCallback(VideoCapture* Capture, ID3D11Texture2D* Texture, const RECT* Rect, uint64_t Time);

typedef struct VideoCapture {
	HRESULT(WINAPI* CreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice*, LPVOID*);
	IGraphicsCaptureItemInterop* ItemInterop;
	IDirect3D11CaptureFramePoolStatics2* FramePoolStatics;
	IDirect3DDevice* Device;
	IGraphicsCaptureItem* Item;
	IDirect3D11CaptureFramePool* FramePool;
	IGraphicsCaptureSession* Session;
	ITypedEventHandler OnCloseHandler;
	ITypedEventHandler OnFrameHandler;
	UINT64 OnCloseToken;
	UINT64 OnFrameToken;
	RECT Rect;
	SIZE CurrentSize;
	BOOL OnlyClientArea;
	HWND Window;
	VideoCapture_OnCloseCallback* CloseCallback;
	VideoCapture_OnFrameCallback* FrameCallback;
} VideoCapture;

void VideoCapture_Init(VideoCapture* Capture, VideoCapture_OnCloseCallback* CloseCallback, VideoCapture_OnFrameCallback* FrameCallback);
void VideoCapture_Done(VideoCapture* Capture);

bool VideoCapture_CreateForWindow(VideoCapture* Capture, ID3D11Device* Device, HWND Window, bool OnlyClientArea);
bool VideoCapture_CreateForMonitor(VideoCapture* Capture, ID3D11Device* Device, HMONITOR Monitor, const RECT* Rect);
void VideoCapture_Start(VideoCapture* Capture, bool WithMouseCursor);
void VideoCapture_Stop(VideoCapture* Capture);
