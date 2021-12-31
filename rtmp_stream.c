#define WIN32_LEAN_AND_MEAN
#include "rtmp_stream.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlwapi.h>

#include <stdarg.h>
#include <intrin.h>

#pragma comment (lib, "OneCore.lib")
#pragma comment (lib, "shlwapi.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "wininet.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

// RTMP https://www.adobe.com/content/dam/acom/en/devnet/rtmp/pdf/rtmp_specification_1.0.pdf
// FLV  https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf
// AMF0 https://www.adobe.com/content/dam/acom/en/devnet/pdf/amf0-file-format-specification.pdf
// AMF3 https://www.adobe.com/content/dam/acom/en/devnet/pdf/amf-file-format-spec.pdf
// NetStream: https://helpx.adobe.com/adobe-media-server/ssaslr/netstream-class.html
// NetConnection: https://helpx.adobe.com/adobe-media-server/ssaslr/netconnection-class.html
// Nick Chadwick: RTMP: A Quick Deep-Dive - https://www.youtube.com/watch?v=AoRepm5ks80

#define RTMP_STATE_ERROR            -1
#define RTMP_STATE_NOT_CONNECTED     0
#define RTMP_STATE_RESOLVING         1 // start dns resolving, wait for dns resolve to finish
#define RTMP_STATE_CONNECTING        2 // called connect(), waiting for connect() call to finish
#define RTMP_STATE_HANDSHAKE         3 // sent C0+C1 handshake, wait to receive receive S0+S1+S2
#define RTMP_STATE_STREAM_CONNECTING 4 // sent C2 handshake, sent SetChunkSize, sent method connect(), wait for _result()
#define RTMP_STATE_STREAM_CREATING   5 // sent method createStream(), wait for _result()
#define RTMP_STATE_STREAM_PUBLISHING 6 // sent method publish(), wait for onStatus()
#define RTMP_STATE_STREAM_READY      7 // sent @setDataFrame() ready to send video & audio packets
#define RTMP_STATE_STREAM_DELETED    8 // sent deleteStream(), wait for send to finish - then do closesocket

#define RTMP_HANDSHAKE_RANDOM_SIZE 1528
#define RTMP_DEFAULT_PORT 1935

#define RTMP_OUT_CHUNK_SIZE 65536   // RTMP outgoing chunk payload max size, 64 KiB
#define RTMP_OUT_ACK_SIZE (1 << 30) // RTMP outgoing data ACK size, 1 GiB (this code does not care about ACK's)

// value probably is not important, they just need to be unique
#define RTMP_TRANSACTION_CONNECT       1
#define RTMP_TRANSACTION_CREATE_STREAM 2
#define RTMP_TRANSACTION_PUBLISH       3
#define RTMP_TRANSACTION_DELETE_STREAM 4

// channel stream id's that will be used
#define RTMP_CHANNEL_CONTROL 2
#define RTMP_CHANNEL_MISC    3
#define RTMP_CHANNEL_AUDIO   4
#define RTMP_CHANNEL_VIDEO   5

// these must use RTMP_CHANNEL_CONTROL
#define RTMP_PACKET_SET_CHUNK_SIZE  1
#define RTMP_PACKET_ACK             3
#define RTMP_PACKET_SET_WINDOW_SIZE 5
#define RTMP_PACKET_SET_PEER_BW     6

// these will use RTMP_CHANNEL_MISC
#define RTMP_PACKET_DATA_AMF0       18
#define RTMP_PACKET_COMMAND_AMF0    20

#define RTMP_PACKET_AUDIO           8 // RTMP_CHANNEL_AUDIO
#define RTMP_PACKET_VIDEO           9 // RTMP_CHANNEL_VIDEO

// returns smallest Pow2 multiple that is >= Value
#define CEIL_POW2(Value, Pow2) (((Value) + (Pow2) - 1) & ~((Pow2) - 1))

// returns ceil(Num/Den)
#define CEIL_DIV(Num, Den) (((Num) - 1) / (Den) + 1)

// little-endian

#define LE_PUT1(Ptr, Value) do { \
	*Ptr++ = (Value);            \
} while (0)

#define LE_PUT2(Ptr, Value) do { \
	*Ptr++ = (Value) >> 0;       \
	*Ptr++ = (Value) >> 8;       \
} while (0)

#define LE_PUT4(Ptr, Value) do { \
	*Ptr++ = (Value) >> 0;       \
	*Ptr++ = (Value) >> 8;       \
	*Ptr++ = (Value) >> 16;      \
	*Ptr++ = (Value) >> 24;      \
} while (0)

#define LE_PUT8(Ptr, Value) do { \
	*Ptr++ = (Value) >> 0;       \
	*Ptr++ = (Value) >> 8;       \
	*Ptr++ = (Value) >> 16;      \
	*Ptr++ = (Value) >> 24;      \
	*Ptr++ = (Value) >> 32;      \
	*Ptr++ = (Value) >> 40;      \
	*Ptr++ = (Value) >> 48;      \
	*Ptr++ = (Value) >> 56;      \
} while (0)

#define LE_GET4(Ptr, Value) do {       \
	Value  = ((uint32_t)*Ptr++) << 0;  \
	Value |= ((uint32_t)*Ptr++) << 8;  \
	Value |= ((uint32_t)*Ptr++) << 16; \
	Value |= ((uint32_t)*Ptr++) << 24; \
} while (0)

// big-endian

#define BE_PUT1(Ptr, Value) do { \
	*Ptr++ = (uint8_t)(Value);   \
} while (0)

#define BE_PUT2(Ptr, Value) do {      \
	*Ptr++ = (uint8_t)((Value) >> 8); \
	*Ptr++ = (uint8_t)((Value) >> 0); \
} while (0)

#define BE_PUT3(Ptr, Value) do {       \
	*Ptr++ = (uint8_t)((Value) >> 16); \
	*Ptr++ = (uint8_t)((Value) >> 8);  \
	*Ptr++ = (uint8_t)((Value) >> 0);  \
} while (0)

#define BE_PUT4(Ptr, Value) do {       \
	*Ptr++ = (uint8_t)((Value) >> 24); \
	*Ptr++ = (uint8_t)((Value) >> 16); \
	*Ptr++ = (uint8_t)((Value) >> 8);  \
	*Ptr++ = (uint8_t)((Value) >> 0);  \
} while (0)

#define BE_PUT8(Ptr, Value) do {       \
	*Ptr++ = (uint8_t)((Value) >> 56); \
	*Ptr++ = (uint8_t)((Value) >> 48); \
	*Ptr++ = (uint8_t)((Value) >> 40); \
	*Ptr++ = (uint8_t)((Value) >> 32); \
	*Ptr++ = (uint8_t)((Value) >> 24); \
	*Ptr++ = (uint8_t)((Value) >> 16); \
	*Ptr++ = (uint8_t)((Value) >> 8);  \
	*Ptr++ = (uint8_t)((Value) >> 0);  \
} while (0)

#define BE_GET1(Ptr, Value) do { \
	Value = *Ptr++;              \
} while (0)

#define BE_GET2(Ptr, Value) do {      \
	Value  = ((uint16_t)*Ptr++) << 8; \
	Value |= ((uint16_t)*Ptr++) << 0; \
} while (0)

#define BE_GET3(Ptr, Value) do { \
	(Value)  = (*(Ptr)++) << 16; \
	(Value) |= (*(Ptr)++) << 8;  \
	(Value) |= (*(Ptr)++);       \
} while (0)

#define BE_GET4(Ptr, Value) do {       \
	Value  = ((uint32_t)*Ptr++) << 24; \
	Value |= ((uint32_t)*Ptr++) << 16; \
	Value |= ((uint32_t)*Ptr++) << 8;  \
	Value |= ((uint32_t)*Ptr++) << 0;  \
} while (0)

#define BE_GET8(Ptr, Value) do {       \
	Value  = ((uint64_t)*Ptr++) << 56; \
	Value |= ((uint64_t)*Ptr++) << 48; \
	Value |= ((uint64_t)*Ptr++) << 40; \
	Value |= ((uint64_t)*Ptr++) << 32; \
	Value |= ((uint64_t)*Ptr++) << 24; \
	Value |= ((uint64_t)*Ptr++) << 16; \
	Value |= ((uint64_t)*Ptr++) << 8;  \
	Value |= ((uint64_t)*Ptr++) << 0;  \
 } while (0)

// AMF0 serialization

#define AMF_PUT_STRING_DATA(Ptr, Str) do { \
	uint32_t Length = sizeof(Str) - 1;     \
	Assert(Length <= 0xffff);              \
	BE_PUT2(Ptr, Length);                  \
	CopyMemory(Ptr, Str, Length);          \
	Ptr += Length;                         \
} while (0)

#define AMF_PUT_STRING_STATIC(Ptr, Str) do { \
	BE_PUT1(Ptr, 2);                         \
	AMF_PUT_STRING_DATA(Ptr, Str);           \
} while (0) 

#define AMF_PUT_STRING_DYNAMIC(Ptr, Str) do { \
	BE_PUT1(Ptr, 2);                          \
	uint8_t* LengthPtr = Ptr;                 \
	uint32_t Length = 0;                      \
	Ptr += 2;                                 \
	for (char* Char = Str; *Char; Length++) { \
		*Ptr++ = *Char++;                     \
	}                                         \
	Assert(Length <= 0xffff);                 \
	BE_PUT2(LengthPtr, Length);               \
} while (0)

#define AMF_PUT_NUMBER(Ptr, Num) do { \
	double Value = (Num);             \
	uint64_t Value64;                 \
	CopyMemory(&Value64, &Value, 8);  \
	BE_PUT1(Ptr, 0);                  \
	BE_PUT8(Ptr, Value64);            \
} while (0)

#define AMF_PUT_BOOL(Ptr, Bool) do { \
	BE_PUT1(Ptr, 1);                 \
	BE_PUT1(Ptr, (Bool) ? 1 : 0);    \
} while (0)

#define AMF_PUT_NULL(Ptr) do { \
	BE_PUT1(Ptr, 5);           \
} while (0)

#define AMF_OBJ_ARRAY(Ptr, Count) do { \
	BE_PUT1(Ptr, 8);                   \
	BE_PUT4(Ptr, Count);               \
} while (0)

#define AMF_OBJ_BEGIN(Ptr) do { \
	BE_PUT1(Ptr, 3);            \
} while (0)

#define AMF_OBJ_END(Ptr) do { \
	BE_PUT3(Ptr, 9);          \
} while (0)

#define AMF_IS_STRING(Ptr) (*(Ptr) == 2)

#define AMF_GET_STRING_LEN(Ptr, Length) BE_GET2(Ptr, Length)

#define AMF_GET_NUMBER(Ptr, Num) do {                \
	Num = 0;                                         \
	if (Ptr[0] == 0) {                               \
		Ptr++;                                       \
		uint64_t Value64;                            \
		BE_GET8(Ptr, Value64);                       \
		CopyMemory(&Num, &Value64, sizeof(Value64)); \
	}                                                \
} while (0)

#define AMF_GET_NULL(Ptr) do { \
	if (Ptr[0] == 5) {         \
		Ptr++;                 \
	}                          \
} while (0)

#ifdef _DEBUG
static void RTMP_DEBUG(const char* Message, ...)
{
	va_list Args;
	va_start(Args, Message);

	char Buffer[1024];
	wvsprintfA(Buffer, Message, Args);
	StrCatA(Buffer, "\n");
	OutputDebugStringA(Buffer);

	va_end(Args);
}
#else
#define RTMP_DEBUG(...) (void)(__VA_ARGS__)
#endif

// RingBuffer stuff

static void RB_Init(RtmpRingBuffer* RingBuffer, uint32_t Size)
{
	// Scenario 1 from Examples at https://docs.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2

	uint8_t* Placeholder1 = VirtualAlloc2(NULL, NULL, 2 * Size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
	uint8_t* Placeholder2 = Placeholder1 + Size;
	Assert(Placeholder1);

	BOOL FreeOk = VirtualFree(Placeholder1, Size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
	Assert(FreeOk);

	HANDLE Section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, Size, NULL);
	Assert(Section);

	uint8_t* View1 = MapViewOfFile3(Section, NULL, Placeholder1, 0, Size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0);
	Assert(View1);

	uint8_t* View2 = MapViewOfFile3(Section, NULL, Placeholder2, 0, Size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0);
	Assert(View2);

	CloseHandle(Section);

	RingBuffer->Buffer = View1;
	RingBuffer->Size = Size;
	RingBuffer->Read = 0;
	RingBuffer->Write = 0;
}

static void RB_Done(RtmpRingBuffer* RingBuffer)
{
	UnmapViewOfFileEx(RingBuffer->Buffer, 0);
	UnmapViewOfFileEx(RingBuffer->Buffer + RingBuffer->Size, 0);
	VirtualFree(RingBuffer->Buffer, 0, MEM_RELEASE);
}

static uint32_t RB_GetUsed(const RtmpRingBuffer* RingBuffer)
{
	return (uint32_t)(RingBuffer->Write - RingBuffer->Read);
}

static uint32_t RB_GetFree(const RtmpRingBuffer* RingBuffer)
{
	return (uint32_t)(RingBuffer->Size - RB_GetUsed(RingBuffer));
}

static bool RB_IsEmpty(const RtmpRingBuffer* RingBuffer)
{
	return RB_GetUsed(RingBuffer) == 0;
}

static bool RB_IsFull(const RtmpRingBuffer* RingBuffer)
{
	return RB_GetFree(RingBuffer) == 0;
}

static uint8_t* RB_BeginRead(RtmpRingBuffer* RingBuffer)
{
	size_t Offset = RingBuffer->Read & (RingBuffer->Size - 1);
	return RingBuffer->Buffer + Offset;
}

static uint8_t* RB_BeginWrite(RtmpRingBuffer* RingBuffer)
{
	size_t Offset = RingBuffer->Write & (RingBuffer->Size - 1);
	return RingBuffer->Buffer + Offset;
}

static void RB_EndRead(RtmpRingBuffer* RingBuffer, uint32_t Size)
{
	Assert(Size <= RB_GetUsed(RingBuffer));
	RingBuffer->Read += Size;
}

static void RB_EndWrite(RtmpRingBuffer* RingBuffer, uint32_t Size)
{
	Assert(Size <= RB_GetFree(RingBuffer));
	RingBuffer->Write += Size;
}

// socket send handling

static void RTMP__BeginSend(SOCKET Socket, RtmpStream* Stream)
{
	uint32_t used = RB_GetUsed(&Stream->Send);
	WSABUF Buffer = { .buf = RB_BeginRead(&Stream->Send), .len = used };
	DWORD Error = WSASend(Socket, &Buffer, 1, NULL, 0, &Stream->SendOv, NULL);
	if (Error == SOCKET_ERROR)
	{
		Error = WSAGetLastError();
		if (Error == WSA_IO_PENDING)
		{
			// send is happening in background
		}
		else if (Error == WSAECONNRESET)
		{
			Assert(!"TODO: connection closed, disconnect");
		}
		else
		{
			Assert(!"TODO: socket error, disconnect");
		}
	}

	Stream->Sending = true;
}

static void RTMP__EndSend(SOCKET Socket, RtmpStream* Stream)
{
	DWORD Transferred;
	DWORD Flags;
	BOOL Ok = WSAGetOverlappedResult(Socket, &Stream->SendOv, &Transferred, TRUE, &Flags);
	ResetEvent(Stream->SendOv.hEvent);
	Assert(Ok);

	Stream->Sending = false;

	Assert(Transferred <= RB_GetUsed(&Stream->Send));
	RB_EndRead(&Stream->Send, Transferred);
}

// RTMP protocol stuff

// fmt=0 chunk size, should fit into one payload, always use timestamp=0
static bool RTMP__WriteChunk(RtmpRingBuffer* Buffer, uint32_t ChunkStreamId, uint32_t MessageType, uint32_t MessageStreamId, const uint8_t* Message, uint32_t MessageSize)
{
	Assert(ChunkStreamId >= 2 && ChunkStreamId < 64);
	Assert(MessageSize <= 0xffffff);
	uint32_t Timestamp = 0;

	uint32_t Available = RB_GetFree(Buffer);
	if (Available < 1 + 3 + 3 + 1 + 4 + MessageSize)
	{
		return false;
	}

	uint8_t* Begin = RB_BeginWrite(Buffer);
	uint8_t* Ptr = Begin;

	BE_PUT1(Ptr, ChunkStreamId);
	BE_PUT3(Ptr, Timestamp);
	BE_PUT3(Ptr, MessageSize);
	BE_PUT1(Ptr, MessageType);
	LE_PUT4(Ptr, MessageStreamId); // lol, little endian for some reason
	CopyMemory(Ptr, Message, MessageSize);
	Ptr += MessageSize;

	RB_EndWrite(Buffer, (uint32_t)(Ptr - Begin));
	return true;
}

// fmt=1 chunk, split into extra fmt=3 chunks
static bool RTMP__SendDeltaChunk(RtmpStream* Stream, uint32_t ChunkStreamId, uint32_t TimestampDelta, uint32_t MessageType, const uint8_t* Extra, uint32_t ExtraSize, const uint8_t* Message, uint32_t MessageSize)
{
	uint32_t TotalSize = ExtraSize + MessageSize;

	Assert(ChunkStreamId >= 2 && ChunkStreamId < 64);
	Assert(TotalSize <= 0xffffff);
	Assert(TimestampDelta < 0xffffff);
	Assert(ExtraSize < RTMP_OUT_CHUNK_SIZE);

	AcquireSRWLockExclusive(&Stream->Lock);

	uint32_t Required = 1 + 3 + 3 + 1;
	if (TotalSize <= RTMP_OUT_CHUNK_SIZE)
	{
		Required += TotalSize;
	}
	else
	{
		uint32_t ChunkCount = CEIL_DIV(TotalSize - RTMP_OUT_CHUNK_SIZE, RTMP_OUT_CHUNK_SIZE);
		Required += ChunkCount; // fmt=3 chunk headers
		Required += TotalSize; // message payload
	}
	RtmpRingBuffer* Buffer = &Stream->Send;

	bool Result = false;
	if (Required <= RB_GetFree(Buffer))
	{
		uint8_t* Begin = RB_BeginWrite(Buffer);
		uint8_t* Ptr = Begin;

		uint32_t ChunkFormat = 1 << 6;
		BE_PUT1(Ptr, ChunkFormat | ChunkStreamId);
		BE_PUT3(Ptr, TimestampDelta);
		BE_PUT3(Ptr, TotalSize);
		BE_PUT1(Ptr, MessageType);

		CopyMemory(Ptr, Extra, ExtraSize);
		Ptr += ExtraSize;
		TotalSize -= ExtraSize;

		const uint8_t* MessageEnd = Message + MessageSize;

		uint32_t PayloadSize = min(TotalSize, RTMP_OUT_CHUNK_SIZE - ExtraSize);
		CopyMemory(Ptr, Message, PayloadSize);
		Ptr += PayloadSize;
		Message += PayloadSize;
		TotalSize -= PayloadSize;

		// make sure first fmt=1 chunk is proper size
		Assert(Ptr <= Begin + 1 + 3 + 3 + 1 + RTMP_OUT_CHUNK_SIZE);

		// rest of chunks will be fmt=3
		ChunkFormat = 3 << 6;
		while (TotalSize != 0)
		{
			BE_PUT1(Ptr, ChunkFormat | ChunkStreamId);

			PayloadSize = min(TotalSize, RTMP_OUT_CHUNK_SIZE);
			CopyMemory(Ptr, Message, PayloadSize);
			Ptr += PayloadSize;
			Message += PayloadSize;
			TotalSize -= PayloadSize;
		}

		Assert(Message == MessageEnd);
		Assert(Ptr == Begin + Required);

		RB_EndWrite(Buffer, (uint32_t)(Ptr - Begin));

		Result = true;
		SetEvent(Stream->DataEvent);
	}
	ReleaseSRWLockExclusive(&Stream->Lock);

	return Result;
}

static bool RTMP__DoHandshake(SOCKET Socket, RtmpStream* Stream)
{
	uint32_t HandshakeSize = 1                // S0
		+ 4 + 4 + RTMP_HANDSHAKE_RANDOM_SIZE  // S1
		+ 4 + 4 + RTMP_HANDSHAKE_RANDOM_SIZE; // S2

	uint32_t Available = RB_GetUsed(&Stream->Recv);
	if (Available < HandshakeSize)
	{
		// not enough data for handshake
		return false;
	}

	RTMP_DEBUG("Received S0+S1+S2 handshake");

	// S0+S1+S2 received, reset received byte count to 0 to exclude header bytes
	Stream->BytesReceived = 0;

	// C2 response == S1
	{
		RTMP_DEBUG("Sending C2 handshake");

		uint8_t* Handshake = RB_BeginRead(&Stream->Recv);

		uint32_t HandshakeResponseSize = 4 + 4 + RTMP_HANDSHAKE_RANDOM_SIZE;
		Assert(HandshakeResponseSize <= RB_GetFree(&Stream->Send));

		uint8_t* Ptr = RB_BeginWrite(&Stream->Send);
		CopyMemory(Ptr, Handshake + 1, HandshakeResponseSize);
		RB_EndWrite(&Stream->Send, HandshakeResponseSize);

		RB_EndRead(&Stream->Recv, HandshakeSize);
	}

	// RTMP_PACKET_SET_CHUNK_SIZE
	{
		RTMP_DEBUG("Setting SetChunkSize to %u", RTMP_OUT_CHUNK_SIZE);

		uint8_t Payload[4];
		uint8_t* Ptr = Payload;

		BE_PUT4(Ptr, RTMP_OUT_CHUNK_SIZE); // chunk payload size

		RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_CONTROL, RTMP_PACKET_SET_CHUNK_SIZE, 0, Payload, sizeof(Payload));
	}

	// RTMP_PACKET_SET_WINDOW_SIZE
	{
		RTMP_DEBUG("Setting WindowAckSize to %u", RTMP_OUT_ACK_SIZE);

		uint8_t Payload[5];
		uint8_t* Ptr = Payload;

		BE_PUT4(Ptr, RTMP_OUT_ACK_SIZE); // ack size
		BE_PUT1(Ptr, 2);                 // limit = dynamic

		RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_CONTROL, RTMP_PACKET_SET_WINDOW_SIZE, 0, Payload, sizeof(Payload));
	}

	// RTMP connect() method
	{
		RTMP_DEBUG("Invoking RTMP connect() method");

		uint8_t Payload[1024];
		uint8_t* Ptr = Payload;

		// path includes '/' as first character, don't need it
		char StreamPath[RTMP_MAX_URL_LENGTH];
		int StreamPathLen = WideCharToMultiByte(CP_UTF8, 0, Stream->UrlComponents.lpszUrlPath + 1, Stream->UrlComponents.dwUrlPathLength - 1, StreamPath, sizeof(StreamPath), NULL, NULL);
		StreamPath[StreamPathLen] = 0;

		AMF_PUT_STRING_STATIC(Ptr, "connect");
		AMF_PUT_NUMBER(Ptr, RTMP_TRANSACTION_CONNECT);
		AMF_OBJ_BEGIN(Ptr);
		AMF_PUT_STRING_DATA(Ptr, "app");      AMF_PUT_STRING_DYNAMIC(Ptr, StreamPath);
		AMF_PUT_STRING_DATA(Ptr, "type");     AMF_PUT_STRING_STATIC(Ptr, "nonprivate");
		AMF_PUT_STRING_DATA(Ptr, "flashVer"); AMF_PUT_STRING_STATIC(Ptr, "FMLE/3.0 (compatible; wstream)");
		AMF_PUT_STRING_DATA(Ptr, "tcUrl");    AMF_PUT_STRING_DYNAMIC(Ptr, Stream->StreamUrl);
		AMF_OBJ_END(Ptr);

		uint32_t PayloadSize = (uint32_t)(Ptr - Payload);
		RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_MISC, RTMP_PACKET_COMMAND_AMF0, 0, Payload, PayloadSize);
	}

	RTMP__BeginSend(Socket, Stream);
	Stream->State = RTMP_STATE_STREAM_CONNECTING;
	RTMP_DEBUG("State ->  RTMP_STATE_STREAM_CONNECTING");

	return true;
}

static void RTMP__DoStreamConnect(SOCKET Socket, RtmpStream* Stream, const uint8_t* Message, uint32_t MessageSize)
{
	if (MessageSize > 3 && AMF_IS_STRING(Message))
	{
		Message++;
		MessageSize--;

		uint32_t StrLen;
		AMF_GET_STRING_LEN(Message, StrLen);
		MessageSize -= 2;

		const char* Str = (const char*)Message;

		if (MessageSize >= StrLen && StrCmpNA(Str, "_result", StrLen) == 0)
		{
			Message += StrLen;
			MessageSize -= StrLen;

			RTMP_DEBUG("Received _result() from RTMP connect() method");

			// RTMP createStream() method
			{
				RTMP_DEBUG("Invoking RTMP createStream() method");

				uint8_t Payload[128];
				uint8_t* Ptr = Payload;

				AMF_PUT_STRING_STATIC(Ptr, "createStream");
				AMF_PUT_NUMBER(Ptr, RTMP_TRANSACTION_CREATE_STREAM);
				AMF_PUT_NULL(Ptr);

				uint32_t PayloadSize = (uint32_t)(Ptr - Payload);
				RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_MISC, RTMP_PACKET_COMMAND_AMF0, 0, Payload, PayloadSize);
			}

			RTMP__BeginSend(Socket, Stream);
			Stream->State = RTMP_STATE_STREAM_CREATING;
			RTMP_DEBUG("State ->  RTMP_STATE_STREAM_CREATING");
			return;
		}
		else if (MessageSize >= StrLen && StrCmpNA(Str, "_error", StrLen) == 0)
		{
			RTMP_DEBUG("Error received from RTMP connect() method");
			Assert(!"TODO: RTMP connect() failed, disconnect");
		}
	}
}

static void RTMP__DoStreamCreate(SOCKET Socket, RtmpStream* Stream, const uint8_t* Message, uint32_t MessageSize)
{
	if (MessageSize > 3 && AMF_IS_STRING(Message))
	{
		Message++;
		MessageSize--;

		uint32_t StrLen;
		AMF_GET_STRING_LEN(Message, StrLen);
		MessageSize -= 2;

		const char* Str = (const char*)Message;
		if (MessageSize >= StrLen + 9 + 1 + 9 && StrCmpNA(Str, "_result", StrLen) == 0)
		{
			Message += StrLen;
			MessageSize -= StrLen;

			double Transaction, StreamId;
			AMF_GET_NUMBER(Message, Transaction);
			AMF_GET_NULL(Message);
			AMF_GET_NUMBER(Message, StreamId);

			Stream->StreamId = (uint32_t)StreamId;

			RTMP_DEBUG("Received _result() from RTMP createStream() method, StreamId=%u", Stream->StreamId);

			// RTMP publish() method
			{
				RTMP_DEBUG("Invoking RTMP publish() method");

				uint8_t Payload[1024];
				uint8_t* Ptr = Payload;

				AMF_PUT_STRING_STATIC(Ptr, "publish");
				AMF_PUT_NUMBER(Ptr, RTMP_TRANSACTION_PUBLISH);
				AMF_PUT_NULL(Ptr);
				AMF_PUT_STRING_DYNAMIC(Ptr, Stream->StreamKey);
				AMF_PUT_STRING_STATIC(Ptr, "live");

				uint32_t PayloadSize = (uint32_t)(Ptr - Payload);
				RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_MISC, RTMP_PACKET_COMMAND_AMF0, Stream->StreamId, Payload, PayloadSize);
			}

			RTMP__BeginSend(Socket, Stream);
			Stream->State = RTMP_STATE_STREAM_PUBLISHING;
			RTMP_DEBUG("State ->  RTMP_STATE_STREAM_PUBLISHING");
			return;
		}
		else if (MessageSize >= StrLen && StrCmpNA(Str, "_error", StrLen) == 0)
		{
			RTMP_DEBUG("Error received from RTMP createStream() method");
			Assert(!"TODO: RTMP createStream() failed, disconnect");
		}
	}
}

static void RTMP__DoStreamPublish(SOCKET Socket, RtmpStream* Stream, const uint8_t* Message, uint32_t MessageSize)
{
	if (MessageSize > 3 && AMF_IS_STRING(Message))
	{
		Message++;
		MessageSize--;

		uint32_t StrLen;
		AMF_GET_STRING_LEN(Message, StrLen);
		MessageSize -= 2;

		const char* Str = (const char*)Message;
		if (MessageSize >= StrLen && StrCmpNA(Str, "onStatus", StrLen) == 0)
		{
			Message += StrLen;
			MessageSize -= StrLen;

			RTMP_DEBUG("Received onStatus() from RTMP publish() method", Stream->StreamId);

			// TODO: this always assumes onStatus means success
			// real check would be to parse object returned
			// code="NetStream.Publish.Start"
			// level="status"

			Stream->State = RTMP_STATE_STREAM_READY;
			RTMP_DEBUG("State ->  RTMP_STATE_STREAM_READY");
			return;
		}
	}

	RTMP_DEBUG("Error received from RTMP publish() method");
	Assert(!"TODO: RTMP publish() failed, disconnect");
}

static bool RTMP__DoChunk(SOCKET Socket, RtmpStream* Stream)
{
	for (;;)
	{
		uint32_t Available = RB_GetUsed(&Stream->Recv);
		if (Available < 1)
		{
			// need at least one byte for chunk
			return false;
		}

		uint8_t* Received = RB_BeginRead(&Stream->Recv);

		uint32_t ChunkStreamId = Received[0] & 0x3f;
		Assert(ChunkStreamId >= 2); // 0 or 1 would mean large ChunkStreamId, which is not supported here

		uint8_t ChunkFormat = Received[0] >> 6;
		uint32_t ChunkHeaderSize = ChunkFormat == 0 ? 11 : ChunkFormat == 1 ? 7 : ChunkFormat == 2 ? 3 : 0;
		if (Available < 1 + ChunkHeaderSize)
		{
			// need more bytes to parse chunk header
			return false;
		}

		RtmpChunk* LastChunk = &Stream->LastChunk[ChunkStreamId];

		uint8_t* Ptr = Received + 1;

		uint32_t Timestamp;
		uint32_t MessageLength;
		uint32_t MessageType;
		uint32_t MessageStreamId;

		if (ChunkFormat == 0)
		{
			BE_GET3(Ptr, Timestamp);
			BE_GET3(Ptr, MessageLength);
			BE_GET1(Ptr, MessageType);
			LE_GET4(Ptr, MessageStreamId);

			LastChunk->Timestamp = Timestamp;
			LastChunk->MessageLength = MessageLength;
			LastChunk->MessageType = MessageType;
			LastChunk->MessageStreamId = MessageStreamId;
		}
		else if (ChunkFormat == 1)
		{
			BE_GET3(Ptr, Timestamp); // this is actually TimestampDelta, but this code doesn't care about received timestamps
			BE_GET3(Ptr, MessageLength);
			BE_GET1(Ptr, MessageType);

			LastChunk->Timestamp = Timestamp;
			LastChunk->MessageLength = MessageLength;
			LastChunk->MessageType = MessageType;
			MessageStreamId = LastChunk->MessageStreamId;
		}
		else if (ChunkFormat == 2)
		{
			BE_GET3(Ptr, Timestamp); // this is actually TimestampDelta

			LastChunk->Timestamp = Timestamp;
			MessageLength = LastChunk->MessageLength;
			MessageType = LastChunk->MessageType;
			MessageStreamId = LastChunk->MessageStreamId;
		}
		else if (ChunkFormat == 3)
		{
			Timestamp = LastChunk->Timestamp; // this is actually TimestampDelta
			MessageLength = LastChunk->MessageLength;
			MessageType = LastChunk->MessageType;
			MessageStreamId = LastChunk->MessageStreamId;
		}
		Assert(Timestamp < 0xffffff); // otherwise extended timestamp is present

		uint32_t ExtraBytes = 0;
		if (MessageLength > Stream->ChunkSize)
		{
			// message is split between multiple chunks, require all of them to be present here
			// TODO: ideally this code should handle non-sequential chunk continuations

			// how many fmt=3 chunks we expect?
			uint32_t ExtraChunkCount = CEIL_DIV(MessageLength - Stream->ChunkSize, Stream->ChunkSize);
			ExtraBytes = ExtraChunkCount; // 1 byte prefix for each extra chunk

			if (Available < 1 + ChunkHeaderSize + ExtraBytes)
			{
				// not enough bytes for chunks
				return false;
			}

			// verify all extra chunks are fmt=3 chunks
			uint32_t ChunkOffset = Stream->ChunkSize;
			for (uint32_t Chunk = 0; Chunk < ExtraChunkCount; Chunk++)
			{
				if (Ptr[ChunkOffset] != ((3 << 6) | ChunkStreamId))
				{
					Assert(!"TODO: non-sequential fmt=3 chunks not supported");
					// TODO: disconnect
				}
				ChunkOffset = 1 + Stream->ChunkSize;
			}

			// un-chunk all data, so it is all sequantial
			uint8_t* Dst = Ptr + Stream->ChunkSize;
			uint8_t* Src = Ptr + Stream->ChunkSize + 1;
			uint32_t UnchunkSize = MessageLength - Stream->ChunkSize;
			while (UnchunkSize != 0)
			{
				uint32_t ChunkSize = min(UnchunkSize, Stream->ChunkSize);
				MoveMemory(Dst, Src, ChunkSize);
				Dst += Stream->ChunkSize;
				Src += Stream->ChunkSize + 1;
				UnchunkSize -= ChunkSize;
			}
		}
		else
		{
			if (Available < 1 + ChunkHeaderSize + MessageLength)
			{
				// need more bytes for message payload
				return false;
			}
		}

		if (MessageType == RTMP_PACKET_SET_WINDOW_SIZE)
		{
			if (MessageLength == 4)
			{
				BE_GET4(Ptr, Stream->WindowSize);
				RTMP_DEBUG("Received SetWindowSize message: %u", Stream->WindowSize);
			}
		}
		else if (MessageType == RTMP_PACKET_SET_CHUNK_SIZE)
		{
			if (MessageLength == 4)
			{
				BE_GET4(Ptr, Stream->ChunkSize);
				RTMP_DEBUG("Received SetChunkSize: %u", Stream->ChunkSize);
			}
		}
		else if (MessageType == RTMP_PACKET_SET_PEER_BW)
		{
			if (MessageLength == 5)
			{
				uint32_t OutgoingBandwidth;
				uint8_t Limit;
				BE_GET4(Ptr, OutgoingBandwidth);
				BE_GET1(Ptr, Limit);

				RTMP_DEBUG("Received SetPeerBandidth: %u, limit %s",
					OutgoingBandwidth,
					Limit == 0 ? "hard" : Limit == 1 ? "soft" : Limit == 2 ? "dynamic" : "UNKNOWN");
			}
		}
		else if (MessageType == RTMP_PACKET_COMMAND_AMF0)
		{
			switch (Stream->State)
			{
			case RTMP_STATE_STREAM_CONNECTING:
				RTMP__DoStreamConnect(Socket, Stream, Ptr, MessageLength);
				break;
			case RTMP_STATE_STREAM_CREATING:
				RTMP__DoStreamCreate(Socket, Stream, Ptr, MessageLength);
				break;
			case RTMP_STATE_STREAM_PUBLISHING:
				RTMP__DoStreamPublish(Socket, Stream, Ptr, MessageLength);
				break;
			}
		}
		else
		{
			// ignore other incoming messages
		}

		RB_EndRead(&Stream->Recv, 1 + ChunkHeaderSize + MessageLength + ExtraBytes);
	}
}

// socket recv handling

static void RTMP__BeginRecv(SOCKET Socket, RtmpStream* Stream)
{
	uint32_t Count = RB_GetFree(&Stream->Recv);
	if (Count == 0)
	{
		Assert(!"TODO: something fishy is going on, cannot receive more data because receive buffer is full");
		// need to disconnect socket here
		return;
	}

	WSABUF Buffer = { .buf = RB_BeginWrite(&Stream->Recv), .len = Count };
	DWORD Flags = 0;
	DWORD Transferred;
	int Error = WSARecv(Socket, &Buffer, 1, &Transferred, &Flags, &Stream->RecvOv, NULL);
	if (Error == SOCKET_ERROR)
	{
		Error = WSAGetLastError();
		if (Error == WSA_IO_PENDING)
		{
			// recv is happening in background
		}
		else if (Error == WSAECONNRESET)
		{
			Assert(!"TODO: connection closed, disconnect");
		}
		else
		{
			Assert(!"TODO: socket error, disconnect");
		}
	}
}

static void RTMP__EndRecv(SOCKET Socket, RtmpStream* Stream)
{
	DWORD Transferred;
	DWORD Flags;
	BOOL Ok = WSAGetOverlappedResult(Socket, &Stream->RecvOv, &Transferred, TRUE, &Flags);
	DWORD Err = WSAGetLastError();
	ResetEvent(Stream->RecvOv.hEvent);
	Assert(Ok);

	if (Transferred == 0)
	{
		Assert(!"TODO: connection closed");
	}

	Assert(Transferred <= RB_GetFree(&Stream->Recv));
	RB_EndWrite(&Stream->Recv, Transferred);

	Stream->TotalByteReceived += Transferred;
	Stream->BytesReceived += Transferred;
	if (Stream->BytesReceived > Stream->WindowSize / 2)
	{
		uint8_t Payload[4];
		uint8_t* Ptr = Payload;

		// this truncates total bytes sent to lower 32-bits, spec does not say what to do when total size >4GiB
		BE_PUT4(Ptr, (uint32_t)Stream->TotalByteReceived);

		RTMP_DEBUG("Sending ACK control message for %u bytes", (uint32_t)Stream->TotalByteReceived);

		AcquireSRWLockExclusive(&Stream->Lock);
		if (RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_CONTROL, RTMP_PACKET_ACK, 0, Payload, sizeof(Payload)))
		{
			Stream->BytesReceived = 0;
		}
		ReleaseSRWLockExclusive(&Stream->Lock);
	}

	bool Repeat;
	do
	{
		switch (Stream->State)
		{
		case RTMP_STATE_HANDSHAKE:
			Repeat = RTMP__DoHandshake(Socket, Stream);
			break;
		case RTMP_STATE_STREAM_CONNECTING:
		case RTMP_STATE_STREAM_CREATING:
		case RTMP_STATE_STREAM_PUBLISHING:
		case RTMP_STATE_STREAM_READY:
		case RTMP_STATE_STREAM_DELETED:
			Repeat = RTMP__DoChunk(Socket, Stream);
			break;
		default:
			// remove all received data
			RB_EndRead(&Stream->Recv, RB_GetUsed(&Stream->Recv));
			Repeat = false;
		}
	}
	while (Repeat);
}

// background processing thread

static DWORD RTMP__Thread(LPVOID Arg)
{
	RtmpStream* Stream = Arg;

	// parse url to know hostname/port to connect
	// ---------------------------------------------------------------------------

	RTMP_DEBUG("parsing url");

	WCHAR StreamUrl[RTMP_MAX_URL_LENGTH];
	int StreamUrlLength = MultiByteToWideChar(CP_UTF8, 0, Stream->StreamUrl, -1, StreamUrl, ARRAYSIZE(StreamUrl));

	WCHAR UrlScheme[32];
	WCHAR UrlHost[RTMP_MAX_URL_LENGTH];
	WCHAR UrlPath[RTMP_MAX_URL_LENGTH];
	Stream->UrlComponents = (URL_COMPONENTSW)
	{
		.dwStructSize = sizeof(Stream->UrlComponents),
		.lpszScheme = UrlScheme,
		.dwSchemeLength = ARRAYSIZE(UrlScheme),
		.lpszHostName = UrlHost,
		.dwHostNameLength = ARRAYSIZE(UrlHost),
		.lpszUrlPath = UrlPath,
		.dwUrlPathLength = ARRAYSIZE(UrlPath),
	};
	if (InternetCrackUrlW(StreamUrl, StreamUrlLength, 0, &Stream->UrlComponents) == FALSE || StrCmpW(UrlScheme, L"rtmp") != 0)
	{
		RTMP_DEBUG("ERROR: cannot parse url or not a rtmp protocol");
		Stream->State = RTMP_STATE_ERROR;
		RTMP_DEBUG("State ->  RTMP_STATE_ERROR");
		return 0;
	}

	WCHAR UrlPort[32];
	wsprintfW(UrlPort, L"%u", Stream->UrlComponents.nPort ? Stream->UrlComponents.nPort : RTMP_DEFAULT_PORT);

	RTMP_DEBUG(" * parsed '%S' scheme, '%S' host, %S port, '%S' path", UrlScheme, UrlHost, UrlPort, UrlPath + 1);

	// do hostname resolving
	// ---------------------------------------------------------------------------

	RTMP_DEBUG("resolving '%S' name", UrlHost);

	Stream->State = RTMP_STATE_RESOLVING;
	RTMP_DEBUG("State ->  RTMP_STATE_RESOLVING");

	ADDRINFOEXW AddressHints =
	{
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	PADDRINFOEXW Addresses;

	HANDLE CancelEvent;
	int Resolve = GetAddrInfoExW(UrlHost, UrlPort, NS_ALL, NULL, &AddressHints, &Addresses, NULL, &Stream->SendOv, NULL, &CancelEvent);
	if (Resolve == WSA_IO_PENDING)
	{
		HANDLE Events[] = { Stream->SendOv.hEvent, Stream->StopEvent };
		DWORD Wait = WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE);
		if (Wait == WAIT_OBJECT_0)
		{
			// ok results are available now
			Resolve = GetAddrInfoExOverlappedResult(&Stream->SendOv);
		}
		else if (Wait == WAIT_OBJECT_0 + 1)
		{
			// quit requested
			GetAddrInfoExCancel(CancelEvent);
			Stream->State = RTMP_STATE_NOT_CONNECTED;
			RTMP_DEBUG("State ->  RTMP_STATE_NOT_CONNECTED");
			return 0;
		}
		else
		{
			Assert(false);
		}
	}

	if (Resolve != NO_ERROR)
	{
		RTMP_DEBUG("ERROR: failed to resolve address");
		Stream->State = RTMP_STATE_ERROR;
		RTMP_DEBUG("State ->  RTMP_STATE_ERROR");
		return 0;
	}

	ResetEvent(Stream->SendOv.hEvent);

	// create & connect socket
	// ---------------------------------------------------------------------------

	RTMP_DEBUG("connecting...");

	Stream->State = RTMP_STATE_CONNECTING;
	RTMP_DEBUG("State ->  RTMP_STATE_CONNECTING");

	ADDRINFOEXW* Address = Addresses;
	SOCKET Socket = INVALID_SOCKET;

	Assert(Address);
	while (Address != NULL)
	{
		WCHAR AddressText[256];
		DWORD AddressTextLength = ARRAYSIZE(AddressText);
		WSAAddressToStringW(Address->ai_addr, (DWORD)Address->ai_addrlen, NULL, AddressText, &AddressTextLength);

		RTMP_DEBUG(" * trying %S address", AddressText);

		Socket = socket(Address->ai_family, Address->ai_socktype, Address->ai_protocol);
		Assert(Socket);

		u_long NonBlocking = 1;
		int Error = ioctlsocket(Socket, FIONBIO, &NonBlocking);
		Assert(Error == 0);

		Error = WSAEventSelect(Socket, Stream->SendOv.hEvent, FD_CONNECT);
		Assert(Error == 0);

		Error = connect(Socket, Address->ai_addr, (int)Address->ai_addrlen);
		if (Error != 0)
		{
			Error = WSAGetLastError();
			if (Error == WSAEWOULDBLOCK)
			{
				HANDLE Events[] = { Stream->SendOv.hEvent, Stream->StopEvent };
				DWORD Wait = WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE);
				if (Wait == WAIT_OBJECT_0)
				{
					// check if it is actually connected
					int ErrorLen = sizeof(Error);
					getsockopt(Socket, SOL_SOCKET, SO_ERROR, (char*)&Error, &ErrorLen);
				}
				else if (Wait == WAIT_OBJECT_0 + 1)
				{
					// quit requested
					closesocket(Socket);
					FreeAddrInfoExW(Addresses);
					Stream->State = RTMP_STATE_NOT_CONNECTED;
					RTMP_DEBUG("State ->  RTMP_STATE_NOT_CONNECTED");
					return 0;
				}
			}
		}

		if (Error == 0)
		{
			// ok we're connected
			RTMP_DEBUG(" * OK");
			break;
		}

		// cannot connect address, will try next one
		Address = Address->ai_next;

		closesocket(Socket);
		Socket = INVALID_SOCKET;
	}

	FreeAddrInfoExW(Addresses);
	ResetEvent(Stream->SendOv.hEvent);

	if (Socket == INVALID_SOCKET)
	{
		RTMP_DEBUG("ERROR: failed to connect to hostname in specified url");
		Stream->State = RTMP_STATE_ERROR;
		RTMP_DEBUG("State ->  RTMP_STATE_ERROR");
		return 0;
	}

	// ---------------------------------------------------------------------------
	// RTMP C0+C1 handshake

	Stream->State = RTMP_STATE_HANDSHAKE;
	RTMP_DEBUG("State ->  RTMP_STATE_HANDSHAKE");

	{
		uint32_t HandshakeSize = 1 + 4 + 4 + RTMP_HANDSHAKE_RANDOM_SIZE;
		Assert(HandshakeSize <= RB_GetFree(&Stream->Send));

		RTMP_DEBUG("Sending C0+C1 handshake");

		uint8_t* Begin = RB_BeginWrite(&Stream->Send);
		uint8_t* Ptr = Begin;

		// C0
		BE_PUT1(Ptr, 3); // version
		// C1
		BE_PUT4(Ptr, 0); // time
		BE_PUT4(Ptr, 0); // always zero
		ZeroMemory(Ptr, RTMP_HANDSHAKE_RANDOM_SIZE); // random bytes, can be zero too!
		Ptr += RTMP_HANDSHAKE_RANDOM_SIZE;

		Assert(Ptr == Begin + HandshakeSize);
		RB_EndWrite(&Stream->Send, HandshakeSize);
	}

	// ---------------------------------------------------------------------------
	// send C0+C1 handshake and start reading response

	RTMP__BeginSend(Socket, Stream);
	RTMP__BeginRecv(Socket, Stream);

	HANDLE Events[] = { Stream->SendOv.hEvent, Stream->RecvOv.hEvent, Stream->DataEvent, Stream->StopEvent };
	for (;;)
	{
		DWORD Wait = WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE);
		if (Wait == WAIT_OBJECT_0)
		{
			RTMP__EndSend(Socket, Stream);
			if (!RB_IsEmpty(&Stream->Send))
			{
				RTMP__BeginSend(Socket, Stream);
			}
		}
		else if (Wait == WAIT_OBJECT_0 + 1)
		{
			RTMP__EndRecv(Socket, Stream);
			RTMP__BeginRecv(Socket, Stream);
		}
		else if (Wait == WAIT_OBJECT_0 + 2)
		{
			// no need to invoke BeginSend if sending operation is already in progress - it will automatically
			// start sending any new data appended to send buffer once previous send finishes
			if (!Stream->Sending && !RB_IsEmpty(&Stream->Send))
			{
				RTMP__BeginSend(Socket, Stream);
			}
			ResetEvent(Stream->DataEvent);
		}
		else if (Wait == WAIT_OBJECT_0 + 3)
		{
			// quit requested
			break;
		}
		else
		{
			Assert(false);
		}
	}

	// TODO: properly handle stop event by sending deleteStream()
	// 
	// RTMP_STATE_STREAM_DELETED
	// 
	// invoke RTMP deleteStream method
	//{
	//	uint8_t Message[1024];

	//	uint8_t* Ptr = Message;
	//	AMF_PUT_STRING(Ptr, "deleteStream"); // command name
	//	AMF_PUT_NUMBER(Ptr, 4);         // transaction id
	//	AMF_PUT_NULL(Ptr);              // user arguments
	//	AMF_PUT_NUMBER(Ptr, OutStreamId);

	//	uint32_t MessageSize = (uint32_t)(Ptr - Message);

	//	uint8_t Packet[1024];
	//	uint32_t PacketSize = RTMP_FullPacket(Packet, RTMP_CHANNEL_MISC, RTMP_PACKET_COMMAND_AMF0, 0, Message, MessageSize);
	//}

	closesocket(Socket);

	return 0;
}

void RTMP_Init(RtmpStream* Stream, const char* Url, const char* Key, uint32_t BufferSize)
{
	WSADATA WsaData;
	int Startup = WSAStartup(MAKEWORD(2, 2), &WsaData);
	Assert(Startup == 0);

	Stream->StopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	Assert(Stream->StopEvent);

	Stream->DataEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	Assert(Stream->DataEvent);

	ZeroMemory(&Stream->RecvOv, sizeof(Stream->RecvOv));
	Stream->RecvOv.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	Assert(Stream->RecvOv.hEvent);

	ZeroMemory(&Stream->SendOv, sizeof(Stream->SendOv));
	Stream->SendOv.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	Assert(Stream->SendOv.hEvent);

	SYSTEM_INFO SysInfo;
	GetSystemInfo(&SysInfo);

	// receive buffer, we don't expect to receive much data
	RB_Init(&Stream->Recv, SysInfo.dwAllocationGranularity);

	// send buffer
	BufferSize = CEIL_POW2(BufferSize, SysInfo.dwAllocationGranularity);
	RB_Init(&Stream->Send, BufferSize);

	StrCpyNA(Stream->StreamUrl, Url, ARRAYSIZE(Stream->StreamUrl));
	StrCpyNA(Stream->StreamKey, Key, ARRAYSIZE(Stream->StreamKey));

	InitializeSRWLock(&Stream->Lock);
	Stream->TotalByteReceived = 0;
	Stream->BytesReceived = 0;
	Stream->WindowSize = 1 << 20; // what's the default value for window size? idk
	Stream->ChunkSize = 128;
	Stream->State = RTMP_STATE_NOT_CONNECTED;
	Stream->Sending = false;
	Stream->VideoTimestamp = 0;
	Stream->AudioTimestamp = 0;
	ZeroMemory(Stream->LastChunk, sizeof(Stream->LastChunk));

	Stream->Thread = CreateThread(NULL, 0, &RTMP__Thread, Stream, 0, NULL);
	Assert(Stream->Thread);
}

void RTMP_Done(RtmpStream* Stream)
{
	SetEvent(Stream->StopEvent);
	WaitForSingleObject(Stream->Thread, INFINITE);
	CloseHandle(Stream->Thread);

	RB_Done(&Stream->Recv);
	RB_Done(&Stream->Send);

	CloseHandle(Stream->SendOv.hEvent);
	CloseHandle(Stream->RecvOv.hEvent);
	CloseHandle(Stream->DataEvent);
	CloseHandle(Stream->StopEvent);

	WSACleanup();
}

bool RTMP_IsStreaming(const RtmpStream* Stream)
{
	return Stream->State == RTMP_STATE_STREAM_READY;
}

bool RTMP_IsError(const RtmpStream* Stream)
{
	return Stream->State == RTMP_STATE_ERROR;
}

void RTMP_SendConfig(RtmpStream* Stream, const RtmpVideoConfig* VideoConfig, const RtmpAudioConfig* AudioConfig)
{
	if (Stream->State != RTMP_STATE_STREAM_READY)
	{
		return;
	}

	uint8_t Payload[1024];
	uint32_t PayloadSize;

	uint8_t* Ptr = Payload;
	{
		RTMP_DEBUG("Sending @setDataFrame data packet");

		AMF_PUT_STRING_STATIC(Ptr, "@setDataFrame");
		AMF_PUT_STRING_STATIC(Ptr, "onMetaData");
		AMF_OBJ_ARRAY(Ptr, 3 + (VideoConfig ? 5 : 0) + (AudioConfig ? 6 : 0));
		AMF_PUT_STRING_DATA(Ptr, "duration"); AMF_PUT_NUMBER(Ptr, 0);
		AMF_PUT_STRING_DATA(Ptr, "filesize"); AMF_PUT_NUMBER(Ptr, 0);
		AMF_PUT_STRING_DATA(Ptr, "encoder");  AMF_PUT_STRING_STATIC(Ptr, "wstream");
		if (VideoConfig)
		{
			AMF_PUT_STRING_DATA(Ptr, "videocodecid");  AMF_PUT_NUMBER(Ptr, 7); // AVC
			AMF_PUT_STRING_DATA(Ptr, "videodatarate"); AMF_PUT_NUMBER(Ptr, VideoConfig->Bitrate);
			AMF_PUT_STRING_DATA(Ptr, "framerate");     AMF_PUT_NUMBER(Ptr, VideoConfig->FrameRate);
			AMF_PUT_STRING_DATA(Ptr, "width");         AMF_PUT_NUMBER(Ptr, VideoConfig->Width);
			AMF_PUT_STRING_DATA(Ptr, "height");        AMF_PUT_NUMBER(Ptr, VideoConfig->Height);
		}
		if (AudioConfig)
		{
			AMF_PUT_STRING_DATA(Ptr, "audiocodecid");    AMF_PUT_NUMBER(Ptr, 10); // AAC
			AMF_PUT_STRING_DATA(Ptr, "audiodatarate");   AMF_PUT_NUMBER(Ptr, AudioConfig->Bitrate);
			AMF_PUT_STRING_DATA(Ptr, "audiosamplerate"); AMF_PUT_NUMBER(Ptr, AudioConfig->SampleRate);
			AMF_PUT_STRING_DATA(Ptr, "audiosamplesize"); AMF_PUT_NUMBER(Ptr, 16);
			AMF_PUT_STRING_DATA(Ptr, "audiochannels");   AMF_PUT_NUMBER(Ptr, AudioConfig->Channels);
			AMF_PUT_STRING_DATA(Ptr, "stereo");          AMF_PUT_BOOL(Ptr, (AudioConfig->Channels == 2));
		}
		AMF_OBJ_END(Ptr);
	}
	PayloadSize = (uint32_t)(Ptr - Payload);
	bool ok = RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_MISC, RTMP_PACKET_DATA_AMF0, Stream->StreamId, Payload, PayloadSize);
	Assert(ok);

	if (VideoConfig && 1 + 1 + 3 + VideoConfig->HeaderSize <= sizeof(Payload))
	{
		Ptr = Payload;
		{
			RTMP_DEBUG("Sending video config packet");

			uint8_t CodecByte = (1 << 4) | 7;
			BE_PUT1(Ptr, CodecByte); // AVC codec
			BE_PUT1(Ptr, 0);         // AVC packet type
			BE_PUT3(Ptr, 0);         // composition time
			CopyMemory(Ptr, VideoConfig->Header, VideoConfig->HeaderSize);
			Ptr += VideoConfig->HeaderSize;
		}
		PayloadSize = (uint32_t)(Ptr - Payload);
		ok = RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_VIDEO, RTMP_PACKET_VIDEO, Stream->StreamId, Payload, PayloadSize);
		Assert(ok);
	}

	if (AudioConfig && 1 + 1 + AudioConfig->HeaderSize <= sizeof(Payload))
	{
		Ptr = Payload;
		{
			RTMP_DEBUG("Sending audio config packet");

			uint8_t CodecByte = (10 << 4) | (3 << 2) | (1 << 1) | 1;
			BE_PUT1(Ptr, CodecByte); // AAC codec
			BE_PUT1(Ptr, 0);         // AAC packet type
			CopyMemory(Ptr, AudioConfig->Header, AudioConfig->HeaderSize);
			Ptr += AudioConfig->HeaderSize;
		}
		PayloadSize = (uint32_t)(Ptr - Payload);
		ok = RTMP__WriteChunk(&Stream->Send, RTMP_CHANNEL_AUDIO, RTMP_PACKET_AUDIO, Stream->StreamId, Payload, PayloadSize);
		Assert(ok);
	}

	SetEvent(Stream->DataEvent);
}

bool RTMP_SendVideo(RtmpStream* Stream, uint64_t DecodeTime, uint64_t PresentTime, uint64_t TimePeriod, const void* VideoData, uint32_t VideoSize, bool IsKeyFrame)
{
	if (Stream->State != RTMP_STATE_STREAM_READY)
	{
		return false;
	}

	uint64_t DecodeTimestamp = DecodeTime * 1000 / TimePeriod;
	uint64_t PresentTimestamp = PresentTime * 1000 / TimePeriod;
	uint32_t CompositionOffset = (uint32_t)(PresentTimestamp - DecodeTimestamp);
	uint32_t Delta = (uint32_t)(DecodeTimestamp - Stream->VideoTimestamp);

	uint8_t CodecByte = ((IsKeyFrame ? 1 : 2) << 4) | 7;

	uint8_t Extra[1+1+3];
	uint8_t* Ptr = Extra;

	BE_PUT1(Ptr, CodecByte);         // AVC codec
	BE_PUT1(Ptr, 1);                 // AVC packet type
	BE_PUT3(Ptr, CompositionOffset);

	Assert(Ptr == Extra + sizeof(Extra));
	if (RTMP__SendDeltaChunk(Stream, RTMP_CHANNEL_VIDEO, Delta, RTMP_PACKET_VIDEO, Extra, sizeof(Extra), VideoData, VideoSize))
	{
		Stream->VideoTimestamp = PresentTimestamp;
		return true;
	}

	return false;
}

bool RTMP_SendAudio(RtmpStream* Stream, uint64_t Time, uint64_t TimePeriod, const void* AudioData, uint32_t AudioSize)
{
	if (Stream->State != RTMP_STATE_STREAM_READY)
	{
		return false;
	}

	uint64_t Timestamp = Time * 1000 / TimePeriod;
	uint32_t Delta = (uint32_t)(Timestamp - Stream->AudioTimestamp);

	uint8_t CodecByte = (10 << 4) | (3 << 2) | (1 << 1) | 1;
	uint8_t Extra[1+1];
	uint8_t* Ptr = Extra;

	BE_PUT1(Ptr, CodecByte); // AAC codec
	BE_PUT1(Ptr, 1);         // AAC packet type

	Assert(Ptr == Extra + sizeof(Extra));
	if (RTMP__SendDeltaChunk(Stream, RTMP_CHANNEL_AUDIO, Delta, RTMP_PACKET_AUDIO, Extra, sizeof(Extra), AudioData, AudioSize))
	{
		Stream->AudioTimestamp = Timestamp;
		return true;
	}

	return false;
}
