#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <d3d11.h>

#include "video_capture.h"
#include "video_encoder.h"

#include "audio_capture.h"
#include "audio_encoder.h"

#include "rtmp_stream.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <intrin.h>

#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "user32.lib")
#pragma comment (lib, "d3d11.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080
#define VIDEO_FRAMERATE 60
#define VIDEO_BITRATE 4000
#define AUDIO_BITRATE 160
#define AUDIO_RATE 48000

#define STREAM_BUFFER_SIZE (((VIDEO_BITRATE + AUDIO_BITRATE) * 1000 / 8) * 2)

typedef struct {
	VideoCapture VideoCapture;
	VideoEncoder VideoEncoder;
	AudioCapture AudioCapture;
	AudioEncoder AudioEncoder;
	RtmpStream Stream;

	LARGE_INTEGER Freq;
	uint64_t NextFrame;
	uint64_t VideoStart;
	uint64_t AudioStart;
} WStream;

static void print(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buffer[1024];
	DWORD length = wvsprintfA(buffer, msg, args);

	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, &written, NULL);

	va_end(args);
}

static void VideoCapture_OnData(VideoCapture* Capture, const VideoCaptureData* Data)
{
	WStream* W = CONTAINING_RECORD(Capture, WStream, VideoCapture);
	
	if (!Data)
	{
		// TODO: window closed;
		return;
	}

	uint64_t Time = Data->Time;
	if (W->VideoStart == 0)
	{
		W->VideoStart = Time;
	}
	Time -= W->VideoStart;

	bool DoEncode = true;

	// encoding framerate limiter
	{
		if (Time * VIDEO_FRAMERATE < W->NextFrame)
		{
			DoEncode = FALSE;
		}
		else
		{
			if (W->NextFrame == 0)
			{
				W->NextFrame = Time * VIDEO_FRAMERATE;
			}
			W->NextFrame += W->Freq.QuadPart;
		}
	}

	if (DoEncode)
	{
		if (!VideoEncoder_Encode(&W->VideoEncoder, Time, W->Freq.QuadPart, &Data->Rect, Data->Texture))
		{
			print("VideoEncoder: dropped frame\n");
		}
	}
}

static void VideoEncoder_OnFrame(VideoEncoder* Encoder, uint64_t DecodeTime, uint64_t PresentTime, uint64_t TimePeriod, bool IsKeyFrame, const void* Data, const uint32_t Size)
{
	WStream* W = CONTAINING_RECORD(Encoder, WStream, VideoEncoder);

	uint64_t dts = DecodeTime * 1000 / TimePeriod;
	uint64_t pts = PresentTime * 1000 / TimePeriod;
	print("V: dts=%u.%03u pts=%u.%03u (%u bytes) %s\n", (uint32_t)(dts / 1000), (uint32_t)(dts % 1000), (uint32_t)(pts / 1000), (uint32_t)(pts % 1000), Size, IsKeyFrame ? "keyframe" : "");

	if (!RTMP_SendVideo(&W->Stream, DecodeTime, PresentTime, TimePeriod, Data, Size, IsKeyFrame))
	{
		print("RTMP: dropped video frame\n");
	}
}

static void AudioCapture_OnData(AudioCapture* Capture, const AudioCaptureData* Data)
{
	WStream* W = CONTAINING_RECORD(Capture, WStream, AudioCapture);

	uint64_t Time = Data->Time;
	if (W->AudioStart == 0)
	{
		W->AudioStart = Time;
	}
	Time -= W->AudioStart;

	AudioEncoder_ProcessInput(&W->AudioEncoder, Time, W->Freq.QuadPart, Data->Samples, Data->SampleCount);

	AudioEncoderOutput EncoderOutput;
	while (AudioEncoder_GetOutput(&W->AudioEncoder, &EncoderOutput))
	{
		uint64_t t = EncoderOutput.Time * 1000 / EncoderOutput.TimePeriod;
		print("A: %u.%03u (%u bytes)\n", (uint32_t)(t / 1000), (uint32_t)(t % 1000), EncoderOutput.Size);

		if (!RTMP_SendAudio(&W->Stream, EncoderOutput.Time, EncoderOutput.TimePeriod, EncoderOutput.Data, EncoderOutput.Size))
		{
			print("RTMP: dropped audio packet\n");
		}

		AudioEncoder_ReleaseOutput(&W->AudioEncoder);
	}
}

void mainCRTStartup()
{
	ID3D11Device* Device;
	DWORD Flags = 0;
#ifdef _DEBUG
	Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	HR(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, Flags, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, NULL));

#ifdef _DEBUG
	ID3D11InfoQueue* Info;
	if (SUCCEEDED(ID3D11Device_QueryInterface(Device, &IID_ID3D11InfoQueue, (LPVOID*)&Info)))
	{
		ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
		ID3D11InfoQueue_Release(Info);
	}
#endif

	// *************************************************
	// WARNING: THIS WILL STREAM YOUR PRIMARY DESKTOP!!!
	// *************************************************

	HMONITOR Monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
	Assert(Monitor);

#if 1
	// http://server:8080/admin/
	const char* StreamUrl = "rtmp://server:1935/live";
	const char* StreamKey = "abc123";
#elif 0
	// Twitch URL from https://stream.twitch.tv/ingests/
	// Twitch key from https://dashboard.twitch.tv/ (Settings -> Stream)
	const char* StreamUrl = "rtmp://sea.contribute.live-video.net/app"; 
	const char* StreamKey = "<<PUT_YOUR_TWITCH_KEY_HERE>>";
#else
	// YouTube url & key from https://youtube.com/livestreaming/stream
	const char* StreamUrl = "rtmp://a.rtmp.youtube.com/live2";
	const char* StreamKey = "<<PUT_YOUR_YOUTUBE_KEY_HERE>>";
#endif

	WStream W;
	QueryPerformanceFrequency(&W.Freq);
	W.NextFrame = 0;
	W.VideoStart = 0;
	W.AudioStart = 0;

	// start connection to rtmp server
	RTMP_Init(&W.Stream, StreamUrl, StreamKey, STREAM_BUFFER_SIZE);

	// initialize video capture
	VideoCapture_Init();

	// currently run on specific monitor, after this call size will be available in W.VideoCapture.Rect member
	bool ok = VideoCapture_CreateForMonitor(&W.VideoCapture, Device, Monitor, NULL, true, &VideoCapture_OnData);
	Assert(ok);

	// setup encoder - currently always scales to specified width/height at specific framerate
	VideoEncoderConfig VideoEnc =
	{
		.InputWidth = W.VideoCapture.Rect.right - W.VideoCapture.Rect.left,
		.InputHeight = W.VideoCapture.Rect.bottom - W.VideoCapture.Rect.top,
		.OutputWidth = VIDEO_WIDTH,
		.OutputHeight = VIDEO_HEIGHT,
		.Bitrate = VIDEO_BITRATE,
		.FramerateNum = VIDEO_FRAMERATE,
		.FramerateDen = 1,
	};
	VideoEncoder_Init(&W.VideoEncoder, Device, &VideoEnc, &VideoEncoder_OnFrame);

	// initializes audio capture, after this call captured format will be available in W.AudioCapture.RecordFormat
	AudioCapture_Create(&W.AudioCapture, &AudioCapture_OnData);

	// setup encoder
	AudioEncoderConfig AudioEnc =
	{
		.InputFormat = W.AudioCapture.Format,
		.Codec = AUDIO_ENCODER_AAC,
		.OutputChannels = 2,
		.OutputSampleRate = AUDIO_RATE,
		.BitrateKbits = AUDIO_BITRATE,
	};
	AudioEncoder_Create(&W.AudioEncoder, &AudioEnc);

	// wait until rtmp protocol finishes handshake and is ready to accept data
	while (!RTMP_IsStreaming(&W.Stream))
	{
		Sleep(1);
	}

	// start the actual capture
	VideoCapture_Start(&W.VideoCapture, true);
	AudioCapture_Start(&W.AudioCapture);

	// send configuration packets to rtmp server - this includes video & audio stream configuration
	uint8_t VideoHeader[1024];
	RtmpVideoConfig VideoStream =
	{
		.Width = VIDEO_WIDTH,
		.Height = VIDEO_HEIGHT,
		.FrameRate = VIDEO_FRAMERATE,
		.Bitrate = VIDEO_BITRATE,
		.Header = VideoHeader,
		.HeaderSize = VideoEncoder_GetHeader(&W.VideoEncoder, VideoHeader, sizeof(VideoHeader)),
	};

	uint8_t AudioHeader[1024];
	RtmpAudioConfig AudioStream =
	{
		.SampleRate = AUDIO_RATE,
		.Bitrate = AUDIO_BITRATE,
		.Channels = 2,
		.Header = AudioHeader,
		.HeaderSize = AudioEncoder_GetHeader(&W.AudioEncoder, AudioHeader, sizeof(AudioHeader)),
	};

	RTMP_SendConfig(&W.Stream, &VideoStream, &AudioStream);

	// loop to do nothing - all the capture & encoding & sending happens in background threads from callbacks
	for (;;)
	{
		Sleep(1);
	}

	// TODO: proper shutdown
	RTMP_Done(&W.Stream);

	AudioEncoder_Destroy(&W.AudioEncoder);

	AudioCapture_Destroy(&W.AudioCapture);
	VideoCapture_Destroy(&W.VideoCapture);

	VideoCapture_Done();

	ExitProcess(0);
}
