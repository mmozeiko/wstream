#pragma once

#include "audio_converter.h"

#include <windows.h>
#include <mftransform.h>

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	AUDIO_ENCODER_WAV,
	AUDIO_ENCODER_MP3, // supported bitrates - 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
	AUDIO_ENCODER_AAC, // supported bitrates - 96, 128, 160, 192
	AUDIO_ENCODER_AC3, // supported bitrates - mono=[64, 80, 96, 112, 128, 160, 192, 224, 256], stereo=[128, 160, 192, 224, 256, 320, 384, 448]
	AUDIO_ENCODER_FLAC,
} AudioEncoderCodec;

typedef struct AudioEncoder {
	AudioConverter Converter;
	IMFTransform* Transform;
	IMFSample* InputSample;
	IMFSample* OutputSample;
	IMFMediaBuffer* InputBuffer;
	IMFMediaBuffer* OutputBuffer;
} AudioEncoder;

typedef struct {
	WAVEFORMATEX* InputFormat;
	AudioEncoderCodec Codec;
	uint32_t OutputChannels;    // 1 or 2
	uint32_t OutputSampleRate;  // typically 44100 or 48000
	uint32_t BitrateKbits;
} AudioEncoderConfig;

typedef struct {
	void* Data;
	uint32_t Size;
	uint64_t Time;
	uint64_t TimePeriod;
} AudioEncoderOutput;

void AudioEncoder_Create(AudioEncoder* Encoder, const AudioEncoderConfig* Config);
void AudioEncoder_Destroy(AudioEncoder* Encoder);

uint32_t AudioEncoder_GetHeader(AudioEncoder* Encoder, uint8_t* Header, uint32_t MaxSize);

void AudioEncoder_ProcessInput(AudioEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount);
bool AudioEncoder_GetOutput(AudioEncoder* Encoder, AudioEncoderOutput* Output);
void AudioEncoder_ReleaseOutput(AudioEncoder* Encoder);
