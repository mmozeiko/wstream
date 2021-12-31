#pragma once

#include <windows.h>
#include <mftransform.h>
#include <d3d11.h>

#include <stdint.h>
#include <stdbool.h>

#define VIDEO_ENCODER_BUFFER_COUNT 8

typedef struct VideoEncoder VideoEncoder;
typedef void VideoEncoder_Callback(VideoEncoder* Encoder, uint64_t DecodeTime, uint64_t PresentTime, uint64_t TimePeriod, bool IsKeyFrame, const void* Data, const uint32_t Size);

typedef struct VideoEncoder {
	IMFTransform* Converter;
	IMFTransform* Encoder;

	HANDLE Stop;
	HANDLE Thread;

	HANDLE InputFree;
	HANDLE InputQueued;
	size_t InputFreeIndex;
	size_t InputQueuedIndex;

	bool Stopping;
	uint32_t InputWidth;
	uint32_t InputHeight;
	uint32_t FramerateNum;
	uint32_t FramerateDen;

	IMFSample* InputSample;
	IMFSample* ConvertedSample[VIDEO_ENCODER_BUFFER_COUNT];
	IMFSample* EncoderInput[VIDEO_ENCODER_BUFFER_COUNT];

	ID3D11DeviceContext* Context;
	ID3D11Texture2D* InputTexture;

	VideoEncoder_Callback* Callback;
} VideoEncoder;

typedef struct {
	uint32_t InputWidth;
	uint32_t InputHeight;
	uint32_t OutputWidth;
	uint32_t OutputHeight;
	uint32_t Bitrate;   // kbit/s
	uint32_t FramerateNum;
	uint32_t FramerateDen;
} VideoEncoderConfig;

void VideoEncoder_Init(VideoEncoder* Encoder, ID3D11Device* Device, const VideoEncoderConfig* Config, VideoEncoder_Callback* Callback);
void VideoEncoder_Done(VideoEncoder* Encoder);

uint32_t VideoEncoder_GetHeader(VideoEncoder* Encoder, uint8_t* Header, uint32_t MaxSize);
bool VideoEncoder_Encode(VideoEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const RECT* Rect, ID3D11Texture2D* Texture);
