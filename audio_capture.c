#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include "audio_capture.h"

#include <avrt.h>
#include <mmdeviceapi.h>

#include <intrin.h>

#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "avrt.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient,         0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioCaptureClient,  0xc8adbd64, 0xe71e, 0x48a0, 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17);
DEFINE_GUID(IID_IAudioRenderClient,   0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);

static DWORD WINAPI AudioCapture__Thread(LPVOID Arg)
{
	AudioCapture* Capture = Arg;

	DWORD TaskIndex = 0;
	HANDLE TaskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
	Assert(TaskHandle);

	IAudioCaptureClient* CaptureClient = Capture->CaptureClient;
	uint32_t FrameSize = Capture->Format->nBlockAlign;

	HANDLE Events[] = { Capture->Event, Capture->Stop };
	while (WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		BYTE* Buffer;
		DWORD Flags;
		UINT32 FrameCount;
		UINT64 Position;
		if (SUCCEEDED(IAudioCaptureClient_GetBuffer(CaptureClient, &Buffer, &FrameCount, &Flags, NULL, &Position)))
		{
			AudioCaptureData Data =
			{
				.Samples = Buffer,
				.SampleCount = FrameCount,
				.Time = Position,
				.Discontinuity = !!(Flags & AUDCLNT_BUFFERFLAGS_SILENT),
			};
			Capture->OnData(Capture, &Data);

			HR(IAudioCaptureClient_ReleaseBuffer(CaptureClient, FrameCount));
		}
	}

	AvRevertMmThreadCharacteristics(TaskHandle);
	return 0;
}

void AudioCapture_Create(AudioCapture* Capture, AudioCapture_OnDataCallback* OnData)
{
	IMMDeviceEnumerator* Enumerator;
	HR(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, &Enumerator));

	IMMDevice* Device;
	HR(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eConsole, &Device));
	IMMDeviceEnumerator_Release(Enumerator);

	Capture->Stop = CreateEventW(NULL, FALSE, FALSE, NULL);
	Assert(Capture->Stop);

	Capture->Event = CreateEventW(NULL, FALSE, FALSE, NULL);
	Assert(Capture->Event);

	// setup playback for silence, otherwise loopback recording does not provide any data when no stream is playing
	{
		IAudioClient* Client;
		HR(IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, &Client));

		WAVEFORMATEX* Format;
		HR(IAudioClient_GetMixFormat(Client, &Format));
		HR(IAudioClient_Initialize(Client, AUDCLNT_SHAREMODE_SHARED, 0, 10*1000*1000, 0, Format, NULL));

		IAudioRenderClient* Render;
		HR(IAudioClient_GetService(Client, &IID_IAudioRenderClient, &Render));

		BYTE* Buffer;
		HR(IAudioRenderClient_GetBuffer(Render, Format->nSamplesPerSec, &Buffer));
		HR(IAudioRenderClient_ReleaseBuffer(Render, Format->nSamplesPerSec, AUDCLNT_BUFFERFLAGS_SILENT));

		IAudioRenderClient_Release(Render);

		Capture->PlayClient = Client;
		CoTaskMemFree(Format);
	}

	// setup loopback recording
	{
		DWORD Flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

		IAudioClient* Client;
		HR(IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, &Client));

		WAVEFORMATEX* Format;
		HR(IAudioClient_GetMixFormat(Client, &Format));
		HR(IAudioClient_Initialize(Client, AUDCLNT_SHAREMODE_SHARED, Flags, 10*1000*1000, 0, Format, NULL));
		HR(IAudioClient_SetEventHandle(Client, Capture->Event));
		HR(IAudioClient_GetService(Client, &IID_IAudioCaptureClient, &Capture->CaptureClient));

		Capture->RecordClient = Client;
		Capture->Format = Format;
	}

	IMMDevice_Release(Device);

	Capture->OnData = OnData;

	Capture->Thread = CreateThread(NULL, 0, &AudioCapture__Thread, Capture, 0, NULL);
	Assert(Capture->Thread);
}

void AudioCapture_Destroy(AudioCapture* Capture)
{
	SetEvent(Capture->Stop);
	WaitForSingleObject(Capture->Thread, INFINITE);
	CloseHandle(Capture->Thread);
	CloseHandle(Capture->Event);
	CloseHandle(Capture->Stop);

	CoTaskMemFree(Capture->Format);
	IAudioCaptureClient_Release(Capture->CaptureClient);
	IAudioClient_Release(Capture->PlayClient);
	IAudioClient_Release(Capture->RecordClient);
}

void AudioCapture_Start(AudioCapture* Capture)
{
	HR(IAudioClient_Start(Capture->PlayClient));
	HR(IAudioClient_Start(Capture->RecordClient));
}

bool AudioCapture_GetData(AudioCapture* Capture, AudioCaptureData* Data)
{
	UINT32 FrameCount;
	if (FAILED(IAudioCaptureClient_GetNextPacketSize(Capture->CaptureClient, &FrameCount)) || FrameCount == 0)
	{
		return false;
	}

	BYTE* Buffer;
	DWORD Flags;
	UINT64 Position;
	if (FAILED(IAudioCaptureClient_GetBuffer(Capture->CaptureClient, &Buffer, &FrameCount, &Flags, NULL, &Position)))
	{
		return false;
	}
	
	Data->Samples = Buffer;
	Data->SampleCount = FrameCount;
	Data->Time = Position;
	Data->Discontinuity = !!(Flags & AUDCLNT_BUFFERFLAGS_SILENT);
	return true;
}

void AudioCapture_Release(AudioCapture* Capture, AudioCaptureData* Data)
{
	HR(IAudioCaptureClient_ReleaseBuffer(Capture->CaptureClient, Data->SampleCount));
}
