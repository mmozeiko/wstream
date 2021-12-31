#pragma once

#include <windows.h>
#include <audioclient.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct AudioCapture AudioCapture;
typedef void AudioCapture_Callback(AudioCapture* Capture, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount);

typedef struct AudioCapture {
	IAudioClient* PlayClient;
	IAudioClient* RecordClient;
	WAVEFORMATEX* PlayFormat;
	WAVEFORMATEX* RecordFormat;
	HANDLE StopEvent;
	HANDLE PlayEvent;
	HANDLE RecordEvent;
	HANDLE PlayThread;
	HANDLE RecordThread;
	AudioCapture_Callback* Callback;
} AudioCapture;

void AudioCapture_Init(AudioCapture* Capture, AudioCapture_Callback* Callback);
void AudioCapture_Done(AudioCapture* Capture);

void AudioCapture_Run(AudioCapture* Capture);
