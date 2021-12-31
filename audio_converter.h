#pragma once

#include <windows.h>
#include <mftransform.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// private
	IMFTransform* Transform;
	IMFSample* InputSample;
	IMFMediaBuffer* InputBuffer;
	uint32_t InputSampleSize;
	uint32_t InputSampleRate;
} AudioConverter;

// AudioConverter converts input samples to 16-bit short format with specified channel & sample rate

void AudioConverter_Create(AudioConverter* Converter, const WAVEFORMATEX* InputFormat, uint32_t MaxInputSampleCount, uint32_t OutputChannels, uint32_t OutputSampleRate);
void AudioConverter_Destroy(AudioConverter* Converter);

void AudioConverter_Flush(AudioConverter* Converter);

void AudioConverter_ProcessInput(AudioConverter* Converter, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount);
bool AudioConverter_GetOutput(AudioConverter* Converter, IMFSample* OutputSample);
