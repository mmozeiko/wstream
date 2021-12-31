#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct IDirect3DDevice             IDirect3DDevice;
typedef struct IGraphicsCaptureItem        IGraphicsCaptureItem;
typedef struct IDirect3D11CaptureFramePool IDirect3D11CaptureFramePool;
typedef struct IDirect3D11CaptureFrame     IDirect3D11CaptureFrame;
typedef struct IGraphicsCaptureSession     IGraphicsCaptureSession;
typedef struct IInspectable                IDispatcherQueueController;

typedef struct ITypedEventHandler { const struct ITypedEventHandlerVtbl* vtbl; } ITypedEventHandler;

// Time is value in QPC units
// Rect can be different from initial Rect because window can be resized
typedef struct {
	// public
	ID3D11Texture2D* Texture;
	uint64_t Time;
	RECT Rect;
	// private
	SIZE Size;
} VideoCaptureData;

typedef struct VideoCapture VideoCapture;

// NOTE: if Data == NULL, that means window/monitor is closed
// otherwise it is called only when new frame is captured, which means
// if window is fully covered, or minimized then no new frames will be captured
typedef void VideoCapture_OnDataCallback(VideoCapture* Capture, const VideoCaptureData* Data);

typedef struct VideoCapture {
	// public
	HANDLE Event;
	bool Closed;
	RECT Rect;
	// private
	IDirect3DDevice* Device;
	IGraphicsCaptureItem* Item;
	IDirect3D11CaptureFramePool* FramePool;
	IDirect3D11CaptureFrame* Frame;
	IGraphicsCaptureSession* Session;
	IDispatcherQueueController* Controller;
	ITypedEventHandler OnFrameHandler;
	ITypedEventHandler OnCloseHandler;
	UINT64 OnFrameToken;
	UINT64 OnCloseToken;
	SIZE CurrentSize;
	bool OnlyClientArea;
	HWND Window;
	VideoCapture_OnDataCallback* OnData;
} VideoCapture;

bool VideoCapture_IsSupported(void);
bool VideoCapture_CanHideMouseCursor(void);

// call once to initialize globals, optionally call Done to release global resources
void VideoCapture_Init(void);
void VideoCapture_Done(void);

// !!! make sure you've called VideoCapture_Init before using Create functions below

// There are three ways to use VideoCapture:
//
// 1. Callback called on background capture thread (CallbackOnThread=true)
//
// 2. Callback called as part of message processing loop (CallbackOnThread=false)
//    in this case thread calling VideoCapture_Create must do message processing loop
//
// 3. Manually call GetData/Release functions to get frame captured
//    in this case you can examine Capture->Event to know when frame is captured
//    Capture->Closed will be set to false on event if window/monitor is closed

// after call to Create, you can access Capture->Rect to know exact size captured

bool VideoCapture_CreateForWindow(VideoCapture* Capture, ID3D11Device* Device, HWND Window, bool OnlyClientArea, bool CallbackOnThread, VideoCapture_OnDataCallback* OnData);
bool VideoCapture_CreateForMonitor(VideoCapture* Capture, ID3D11Device* Device, HMONITOR Monitor, const RECT* Rect, bool CallbackOnThread, VideoCapture_OnDataCallback* OnData);
void VideoCapture_Destroy(VideoCapture* Capture);

void VideoCapture_Start(VideoCapture* Capture, bool WithMouseCursor);

// do not call these if you use callback
bool VideoCapture_GetData(VideoCapture* Capture, VideoCaptureData* Data);
void VideoCapture_Release(VideoCapture* Capture, VideoCaptureData* Data);
