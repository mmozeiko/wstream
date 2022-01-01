#pragma once

#include <windows.h>
#include <audioclient.h>

#include <stdint.h>
#include <stdbool.h>

// Time is value in QPC units
typedef struct {
	void* Samples;
	uint32_t SampleCount;
	uint64_t Time;
} AudioCaptureData;

typedef struct AudioCapture AudioCapture;
typedef void AudioCapture_OnDataCallback(AudioCapture* Capture, const AudioCaptureData* Data);

typedef struct AudioCapture {
	// public
	WAVEFORMATEX* Format;
	HANDLE Event;
	// private
	IAudioClient* PlayClient;
	IAudioClient* RecordClient;
	IAudioCaptureClient* CaptureClient;
	HANDLE Stop;
	HANDLE Thread;
	AudioCapture_OnDataCallback* OnData;
} AudioCapture;

// !!! make sure you've called CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) before using these functions

// There are two ways to use AudioCapture:
// 1. Callback driven - callback will be called on background thread whenever data is captured
// 2. Manually - call GetData/Release functions manually to get any data captured
//               in this case you can wait on Capture->Event to know when new data is captured
// capture buffer has duration of 1 second, so make sure you process data more often than once a second
// after call to Create, you can access Capture->Format to examine in what format audio is captured
void AudioCapture_Create(AudioCapture* Capture, AudioCapture_OnDataCallback* OnData);
void AudioCapture_Destroy(AudioCapture* Capture);

void AudioCapture_Start(AudioCapture* Capture);

// do not call these if you use callback
bool AudioCapture_GetData(AudioCapture* Capture, AudioCaptureData* Data);
void AudioCapture_Release(AudioCapture* Capture, AudioCaptureData* Data);
