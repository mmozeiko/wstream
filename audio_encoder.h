#pragma once

#include <windows.h>
#include <mftransform.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct AudioEncoder AudioEncoder;
typedef void AudioEncoder_Callback(AudioEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const void* Data, const uint32_t Size);

typedef struct AudioEncoder {
	IMFTransform* Resampler;
	IMFTransform* Encoder;

	IMFSample* InputSample;
	IMFMediaBuffer* InputBuffer;

	IMFSample* ResampledSample;
	IMFMediaBuffer* ResampledBuffer;

	IMFSample* OutputSample;
	IMFMediaBuffer* OutputBuffer;

	uint32_t FrameSize;
	uint32_t SampleRate;

	AudioEncoder_Callback* Callback;
} AudioEncoder;

typedef struct {
	WAVEFORMATEX* Format;
	uint32_t Bitrate;   // kbit/s
} AudioEncoderConfig;

void AudioEncoder_Init(AudioEncoder* Encoder, const AudioEncoderConfig* Config, AudioEncoder_Callback* Callback);
void AudioEncoder_Done(AudioEncoder* Encoder);

uint32_t AudioEncoder_GetHeader(AudioEncoder* Encoder, uint8_t* Header, uint32_t MaxSize);
void AudioEncoder_Input(AudioEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount);
