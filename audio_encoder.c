#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "audio_encoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>

#include <intrin.h>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "wmcodecdspuuid.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define MF_UNITS_PER_SECOND 10000000ULL
#define MF64(high, low) (((UINT64)high << 32) | (low))

#define AUDIO_ENCODER_RATE     48000
#define AUDIO_ENCODER_CHANNELS 2

void AudioEncoder_Init(AudioEncoder* Encoder, const AudioEncoderConfig* Config, AudioEncoder_Callback* Callback)
{
	HR(CoCreateInstance(&CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (LPVOID*)&Encoder->Resampler));
	HR(CoCreateInstance(&CLSID_AACMFTEncoder, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (LPVOID*)&Encoder->Encoder));

	// create audio input/output types
	{
		IMFMediaType* ResamplerInput;
		HR(MFCreateMediaType(&ResamplerInput));
		HR(MFInitMediaTypeFromWaveFormatEx(ResamplerInput, Config->Format, sizeof(*Config->Format) + Config->Format->cbSize));
		
		WAVEFORMATEX OutputFormat =
		{
			.wFormatTag = WAVE_FORMAT_PCM,
			.nChannels = (WORD)AUDIO_ENCODER_CHANNELS,
			.nSamplesPerSec = AUDIO_ENCODER_RATE,
			.wBitsPerSample = sizeof(short) * 8,
		};
		OutputFormat.nBlockAlign = OutputFormat.nChannels * OutputFormat.wBitsPerSample / 8;
		OutputFormat.nAvgBytesPerSec = OutputFormat.nSamplesPerSec * OutputFormat.nBlockAlign;

		IMFMediaType* EncoderInput;
		HR(MFCreateMediaType(&EncoderInput));
		HR(MFInitMediaTypeFromWaveFormatEx(EncoderInput, &OutputFormat, sizeof(OutputFormat)));

		IMFMediaType* EncoderOutput;
		HR(MFCreateMediaType(&EncoderOutput));
		HR(IMFMediaType_SetGUID(EncoderOutput, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio));
		HR(IMFMediaType_SetGUID(EncoderOutput, &MF_MT_SUBTYPE, &MFAudioFormat_AAC));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_ENCODER_RATE));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_AUDIO_NUM_CHANNELS, AUDIO_ENCODER_CHANNELS));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, Config->Bitrate * 1000 / 8));

		// setup audio resampler & encoder types
		HR(IMFTransform_SetInputType(Encoder->Resampler, 0, ResamplerInput, 0));
		HR(IMFTransform_SetOutputType(Encoder->Resampler, 0, EncoderInput, 0));

		HR(IMFTransform_SetOutputType(Encoder->Encoder, 0, EncoderOutput, 0));
		HR(IMFTransform_SetInputType(Encoder->Encoder, 0, EncoderInput, 0));

		IMFMediaType_Release(ResamplerInput);
		IMFMediaType_Release(EncoderInput);
		IMFMediaType_Release(EncoderOutput);
	}

	// check that audio resmapler & encoder allows to pass our allocated buffers
	DWORD EncoderOutputSize = 0;
	{
		MFT_OUTPUT_STREAM_INFO Info;
		HR(IMFTransform_GetOutputStreamInfo(Encoder->Resampler, 0, &Info));
		Assert((Info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) == 0);
		Assert(Info.cbAlignment <= 1);

		HR(IMFTransform_GetOutputStreamInfo(Encoder->Encoder, 0, &Info));
		Assert((Info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) == 0);
		Assert(Info.cbAlignment <= 1);
		EncoderOutputSize = Info.cbSize;
	}

	// allocate input
	{
		IMFSample* InputSample;
		IMFMediaBuffer* InputBuffer;
		HR(MFCreateSample(&InputSample));
		HR(MFCreateMemoryBuffer(1024 * 1024, &InputBuffer)); // TODO: 1 second for input
		HR(IMFSample_AddBuffer(InputSample, InputBuffer));

		Encoder->InputSample = InputSample;
		Encoder->InputBuffer = InputBuffer;
	}

	// allocate resampled
	{
		IMFSample* ResampledSample;
		IMFMediaBuffer* ResampledBuffer;
		HR(MFCreateSample(&ResampledSample));
		HR(MFCreateMemoryBuffer(1024 * 1024, &ResampledBuffer)); // TODO: 1 second for input
		HR(IMFSample_AddBuffer(ResampledSample, ResampledBuffer));

		Encoder->ResampledSample = ResampledSample;
		Encoder->ResampledBuffer = ResampledBuffer;
	}

	// allocate output
	{
		IMFSample* OutputSample;
		IMFMediaBuffer* OutputBuffer;
		HR(MFCreateSample(&OutputSample));
		HR(MFCreateMemoryBuffer(EncoderOutputSize, &OutputBuffer));
		HR(IMFSample_AddBuffer(OutputSample, OutputBuffer));

		Encoder->OutputSample = OutputSample;
		Encoder->OutputBuffer = OutputBuffer;
	}

	HR(IMFTransform_ProcessMessage(Encoder->Resampler, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
	HR(IMFTransform_ProcessMessage(Encoder->Encoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

	Encoder->FrameSize = Config->Format->nBlockAlign;
	Encoder->SampleRate = Config->Format->nSamplesPerSec;
	Encoder->Callback = Callback;
}

void AudioEncoder_Done(AudioEncoder* Encoder)
{
	IMFMediaBuffer_Release(Encoder->OutputBuffer);
	IMFMediaBuffer_Release(Encoder->InputBuffer);
	IMFSample_Release(Encoder->OutputSample);
	IMFSample_Release(Encoder->ResampledSample);
	IMFSample_Release(Encoder->InputSample);
	IMFTransform_Release(Encoder->Encoder);
	IMFTransform_Release(Encoder->Resampler);

	HR(MFShutdown());
}

uint32_t AudioEncoder_GetHeader(AudioEncoder* Encoder, uint8_t* Header, uint32_t MaxSize)
{
	IMFMediaType* OutputType;
	HR(IMFTransform_GetOutputCurrentType(Encoder->Encoder, 0, &OutputType));

	UINT32 HeaderSize;
	HR(IMFMediaType_GetBlobSize(OutputType, &MF_MT_USER_DATA, &HeaderSize));

	if (HeaderSize <= MaxSize)
	{
		HR(IMFMediaType_GetBlob(OutputType, &MF_MT_USER_DATA, Header, MaxSize, &HeaderSize));

		// see HEAACWAVEFORMAT docs for description of what Header contains
		// MF_MT_USER_DATA returns only bytes after wfInfo.wfx member
		uint32_t SkipBytes = sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX);
		MoveMemory(Header, Header + SkipBytes, HeaderSize - SkipBytes);
		HeaderSize -= SkipBytes;
	}

	IMFMediaType_Release(OutputType);
	return HeaderSize;
}

void AudioEncoder_Input(AudioEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount)
{
	IMFMediaBuffer* InputBuffer = Encoder->InputBuffer;
	IMFSample* InputSample = Encoder->InputSample;

	BYTE* BufferData;
	DWORD MaxLength;
	HR(IMFMediaBuffer_Lock(InputBuffer, &BufferData, &MaxLength, NULL));

	DWORD BufferSize = SampleCount * Encoder->FrameSize;
	Assert(BufferSize <= MaxLength);
	CopyMemory(BufferData, Samples, SampleCount * Encoder->FrameSize);

	HR(IMFMediaBuffer_Unlock(InputBuffer));
	HR(IMFMediaBuffer_SetCurrentLength(InputBuffer, BufferSize));

	// setup input & duration
	HR(IMFSample_SetSampleDuration(InputSample, MFllMulDiv(SampleCount, MF_UNITS_PER_SECOND, Encoder->SampleRate, 0)));
	HR(IMFSample_SetSampleTime(InputSample, MFllMulDiv(Time, MF_UNITS_PER_SECOND, TimePeriod, 0)));

	// do the resampling if needed
	HR(IMFTransform_ProcessInput(Encoder->Resampler, 0, InputSample, 0));

	// process resampler output
	for (;;)
	{
		DWORD Status;
		MFT_OUTPUT_DATA_BUFFER Resampled = { .pSample = Encoder->ResampledSample };
		HRESULT hr = IMFTransform_ProcessOutput(Encoder->Resampler, 0, 1, &Resampled, &Status);
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
		{
			break;
		}
		Assert(SUCCEEDED(hr));

		//DWORD ResampledLength;
		//IMFMediaBuffer_GetCurrentLength(Encoder->ResampledBuffer, &ResampledLength);
		//HR(IMFSample_SetSampleTime(Encoder->ResampledSample, MFllMulDiv(Encoder->InputSampleCount, MF_UNITS_PER_SECOND, AUDIO_ENCODER_RATE, 0)));
		//Encoder->InputSampleCount += ResampledLength / AUDIO_ENCODER_CHANNELS / sizeof(short);

		// do the encoding
		HR(IMFTransform_ProcessInput(Encoder->Encoder, 0, Encoder->ResampledSample, 0));
		//IMFSample_Release(Output.pSample);

		// process encoder output
		for (;;)
		{
			DWORD Status;
			MFT_OUTPUT_DATA_BUFFER Output = { .pSample = Encoder->OutputSample };
			HRESULT hr = IMFTransform_ProcessOutput(Encoder->Encoder, 0, 1, &Output, &Status);
			if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
			{
				break;
			}
			Assert(SUCCEEDED(hr));

			// call the callback
			{
				BYTE* Data;
				DWORD Size;
				if (SUCCEEDED(IMFMediaBuffer_Lock(Encoder->OutputBuffer, &Data, NULL, &Size)))
				{
					LONGLONG Time;
					HR(IMFSample_GetSampleTime(Encoder->OutputSample, &Time));
					Encoder->Callback(Encoder, Time, MF_UNITS_PER_SECOND, Data, Size);

					HR(IMFMediaBuffer_Unlock(Encoder->OutputBuffer));
				}
			}
			//IMFSample_Release(Output.pSample);
		}
	}
}
