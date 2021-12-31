#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include "audio_capture.h"

#include <avrt.h>
#include <mmdeviceapi.h>

#include <intrin.h>

#pragma comment (lib, "avrt.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define PLAYBACK_BUFFER_DURATION 1000000  // 100 msec
#define CAPTURE_BUFFER_DURATION   500000 // 50 msec

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient, 0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioCaptureClient, 0xc8adbd64, 0xe71e, 0x48a0, 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17);
DEFINE_GUID(IID_IAudioRenderClient, 0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);

static DWORD WINAPI AudioCapture__PlayThread(LPVOID Arg)
{
	DWORD TaskIndex = 0;
	HANDLE TaskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
	Assert(TaskHandle);

	AudioCapture* Capture = Arg;

	IAudioRenderClient* Client;
	HR(IAudioClient_GetService(Capture->PlayClient, &IID_IAudioRenderClient, &Client));

	UINT32 BufferSize;
	HR(IAudioClient_GetBufferSize(Capture->PlayClient, &BufferSize));

	DWORD FrameSize = Capture->PlayFormat->nBlockAlign;

	HANDLE Events[] = { Capture->PlayEvent, Capture->StopEvent };
	while (WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		UINT32 BufferFilled;
		HR(IAudioClient_GetCurrentPadding(Capture->PlayClient, &BufferFilled));

		UINT32 FrameCount = BufferSize - BufferFilled;
		if (FrameCount != 0)
		{
			BYTE* Data;
			if (SUCCEEDED(IAudioRenderClient_GetBuffer(Client, FrameCount, &Data)))
			{
				ZeroMemory(Data, FrameCount * FrameSize);
				IAudioRenderClient_ReleaseBuffer(Client, FrameCount, 0);
			}
		}
	}

	IAudioRenderClient_Release(Client);
	AvRevertMmThreadCharacteristics(TaskHandle);
	return 0;
}

static DWORD WINAPI AudioCapture__RecordThread(LPVOID Arg)
{
	DWORD TaskIndex = 0;
	HANDLE TaskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
	Assert(TaskHandle);

	AudioCapture* Capture = Arg;

	IAudioCaptureClient* Client;
	HR(IAudioClient_GetService(Capture->RecordClient, &IID_IAudioCaptureClient, &Client));

	DWORD FrameSize = Capture->RecordFormat->nBlockAlign;

	LARGE_INTEGER Freq;
	QueryPerformanceFrequency(&Freq);

	HANDLE Events[] = { Capture->RecordEvent, Capture->StopEvent };
	while (WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		BYTE* Buffer;
		DWORD Flags;
		UINT32 FrameCount;
		UINT64 Time;
		if (SUCCEEDED(IAudioCaptureClient_GetBuffer(Client, &Buffer, &FrameCount, &Flags, NULL, &Time)))
		{
			Capture->Callback(Capture, Time, Freq.QuadPart, Buffer, FrameCount);

			HR(IAudioCaptureClient_ReleaseBuffer(Client, FrameCount));
		}
	}

	IAudioCaptureClient_Release(Client);
	AvRevertMmThreadCharacteristics(TaskHandle);
	return 0;
}

void AudioCapture_Init(AudioCapture* Capture, AudioCapture_Callback* Callback)
{
	IMMDeviceEnumerator* Enumerator;
	HR(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (LPVOID*)&Enumerator));

	IMMDevice* Device;
	HR(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eConsole, &Device));
	IMMDeviceEnumerator_Release(Enumerator);

	Capture->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	Assert(Capture->StopEvent);

	// setup playback for silence, otherwise loopback recording does not provide any data
	{
		Capture->PlayEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
		Assert(Capture->PlayEvent);

		IAudioClient* Client;
		HR(IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (LPVOID*)&Client));

		WAVEFORMATEX* Format;
		HR(IAudioClient_GetMixFormat(Client, &Format));
		HR(IAudioClient_Initialize(Client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, PLAYBACK_BUFFER_DURATION, 0, Format, NULL));
		HR(IAudioClient_SetEventHandle(Client, Capture->PlayEvent));

		Capture->PlayClient = Client;
		Capture->PlayFormat = Format;
		Capture->PlayThread = CreateThread(NULL, 0, &AudioCapture__PlayThread, Capture, 0, NULL);
		Assert(Capture->PlayThread);
	}

	// setup loopback recording
	{
		Capture->RecordEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
		Assert(Capture->RecordEvent);

		IAudioClient* Client;
		HR(IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (LPVOID*)&Client));

		WAVEFORMATEX* Format;
		HR(IAudioClient_GetMixFormat(Client, &Format));
		HR(IAudioClient_Initialize(Client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, CAPTURE_BUFFER_DURATION, 0, Format, NULL));
		HR(IAudioClient_SetEventHandle(Client, Capture->RecordEvent));

		Capture->RecordClient = Client;
		Capture->RecordFormat = Format;
		Capture->RecordThread = CreateThread(NULL, 0, &AudioCapture__RecordThread, Capture, 0, NULL);
		Assert(Capture->RecordThread);
	}

	IMMDevice_Release(Device);

	Capture->Callback = Callback;
}

void AudioCapture_Done(AudioCapture* Capture)
{
	HR(IAudioClient_Stop(Capture->PlayClient));
	HR(IAudioClient_Stop(Capture->RecordClient));

	SetEvent(Capture->StopEvent);

	HANDLE Threads[] = { Capture->PlayThread, Capture->RecordThread };
	WaitForMultipleObjects(ARRAYSIZE(Threads), Threads, TRUE, INFINITE);

	CloseHandle(Capture->PlayThread);
	CloseHandle(Capture->RecordThread);
	CloseHandle(Capture->PlayEvent);
	CloseHandle(Capture->RecordEvent);
	CloseHandle(Capture->StopEvent);

	CoTaskMemFree(Capture->PlayFormat);
	CoTaskMemFree(Capture->RecordFormat);
	IAudioClient_Release(Capture->PlayClient);
	IAudioClient_Release(Capture->RecordClient);
}

void AudioCapture_Run(AudioCapture* Capture)
{
	HR(IAudioClient_Start(Capture->PlayClient));
	HR(IAudioClient_Start(Capture->RecordClient));
}
