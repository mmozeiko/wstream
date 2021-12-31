#pragma once

#include <windows.h>
#include <mftransform.h>

#include <stdint.h>

typedef struct {
	// private
	IMFTransform* Transform;
	IMFSample* InputSample;
	IMFSample* OutputSample;
	IMFMediaBuffer* InputBuffer;
	IMFMediaBuffer* OutputBuffer;
	uint32_t InputSampleSize;
	uint32_t InputSampleRate;
} AudioConverter;

// !!! make sure you've called MFStartup(MF_VERSION, MFSTARTUP_LITE) before using these functions

// AudioConverter converts input samples to 16-bit short format with specified channel & sample rate

void AudioConverter_Create(AudioConverter* Converter, const WAVEFORMATEX* InputFormat, uint32_t BufferSampleCount, uint32_t OutputChannels, uint32_t OutputSampleRate);
void AudioConverter_Destroy(AudioConverter* Converter);

void AudioConverter_Flush(AudioConverter* Converter);

void AudioConverter_SendInput(AudioConverter* Converter, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount);
IMFSample* AudioConverter_GetOutput(AudioConverter* Converter);
