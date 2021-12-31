#pragma once

#include <windows.h>
#include <wininet.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RTMP_MAX_URL_LENGTH 256
#define RTMP_MAX_KEY_LENGTH 256

typedef struct {
	uint8_t* Buffer;
	size_t Size;
	size_t Read;
	size_t Write;
} RtmpRingBuffer;

typedef struct {
	uint32_t Timestamp;
	uint32_t MessageLength;
	uint32_t MessageType;
	uint32_t MessageStreamId;
} RtmpChunk;

typedef struct {
	HANDLE Thread;
	HANDLE StopEvent;
	HANDLE DataEvent;

	RtmpRingBuffer Recv;
	RtmpRingBuffer Send;

	OVERLAPPED RecvOv;
	OVERLAPPED SendOv;

	SRWLOCK Lock;
	uint64_t TotalByteReceived;
	uint32_t BytesReceived;
	uint32_t WindowSize;
	uint32_t ChunkSize;
	uint32_t StreamId;
	uint32_t State;
	bool Sending;

	RtmpChunk LastChunk[64];

	uint64_t VideoTimestamp;
	uint64_t AudioTimestamp;

	char StreamUrl[RTMP_MAX_URL_LENGTH];
	char StreamKey[RTMP_MAX_KEY_LENGTH];
	URL_COMPONENTSW UrlComponents;
} RtmpStream;

// only H264/AVC codec supported
typedef struct {
	uint32_t Width;
	uint32_t Height;
	uint32_t FrameRate;
	uint32_t Bitrate;   // kbit/s
	// AVCDecoderConfigurationRecord from ISO 14496-15 spec
	const void* Header;
	size_t HeaderSize;
} RtmpVideoConfig;

// only AAC codec supported
typedef struct {
	uint32_t SampleRate; // Hz
	uint32_t Bitrate;    // kbit/s
	uint32_t Channels;   // 1 or 2
	// AudioSpecificConfig from ISO/IEC 14496-3 spec
	const void* Header;
	size_t HeaderSize;
} RtmpAudioConfig;

// buffer size is for outgoing buffer - if it will be full then frames will be dropped
void RTMP_Init(RtmpStream* Stream, const char* Url, const char* Key, uint32_t BufferSize);
void RTMP_Done(RtmpStream* Stream);

bool RTMP_IsStreaming(const RtmpStream* Stream);
bool RTMP_IsError(const RtmpStream* Stream);

// send config only once after IsStreaming returns true
void RTMP_SendConfig(RtmpStream* Stream, const RtmpVideoConfig* VideoConfig, const RtmpAudioConfig* AudioConfig);

// these return false is there is no more place in outgoing buffer
// can be called from different threads
bool RTMP_SendVideo(RtmpStream* Stream, uint64_t DecodeTime, uint64_t PresentTime, uint64_t TimePeriod, const void* VideoData, uint32_t VideoSize, bool IsKeyFrame);
bool RTMP_SendAudio(RtmpStream* Stream, uint64_t Time, uint64_t TimePeriod, const void* AudioData, uint32_t AudioSize);
