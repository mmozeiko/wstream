#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "audio_converter.h"
#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>

#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "wmcodecdspuuid.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define MF_UNITS_PER_SECOND 10000000ULL

void AudioConverter_Create(AudioConverter* Converter, const WAVEFORMATEX* InputFormat, uint32_t BufferSampleCount, uint32_t OutputChannels, uint32_t OutputSampleRate)
{
	HR(CoCreateInstance(&CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, &Converter->Transform));

	WAVEFORMATEX OutputFormat =
	{
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = (WORD)OutputChannels,
		.nSamplesPerSec = OutputSampleRate,
		.wBitsPerSample = sizeof(short) * 8,
	};
	OutputFormat.nBlockAlign = OutputFormat.nChannels * OutputFormat.wBitsPerSample / 8;
	OutputFormat.nAvgBytesPerSec = OutputFormat.nSamplesPerSec * OutputFormat.nBlockAlign;

	IMFMediaType* InputType;
	HR(MFCreateMediaType(&InputType));
	HR(MFInitMediaTypeFromWaveFormatEx(InputType, InputFormat, sizeof(*InputFormat) + InputFormat->cbSize));
	HR(IMFTransform_SetInputType(Converter->Transform, 0, InputType, 0));
	IMFMediaType_Release(InputType);

	IMFMediaType* OutputType;
	HR(MFCreateMediaType(&OutputType));
	HR(MFInitMediaTypeFromWaveFormatEx(OutputType, &OutputFormat, sizeof(OutputFormat)));
	HR(IMFTransform_SetOutputType(Converter->Transform, 0, OutputType, 0));
	IMFMediaType_Release(OutputType);

	// verify that we can provide pre-allocated input & output memory
	{
		MFT_INPUT_STREAM_INFO InputInfo;
		HR(IMFTransform_GetInputStreamInfo(Converter->Transform, 0, &InputInfo));
		Assert((InputInfo.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) == 0);
		Assert(InputInfo.cbAlignment <= 1);

		MFT_OUTPUT_STREAM_INFO OutputInfo;
		HR(IMFTransform_GetOutputStreamInfo(Converter->Transform, 0, &OutputInfo));
		Assert((OutputInfo.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) == 0);
		Assert(OutputInfo.cbAlignment <= 1);
	}

	// allocate input
	{
		IMFSample* InputSample;
		IMFMediaBuffer* InputBuffer;
		HR(MFCreateSample(&InputSample));
		HR(MFCreateMemoryBuffer(BufferSampleCount * InputFormat->nBlockAlign, &InputBuffer));
		HR(IMFSample_AddBuffer(InputSample, InputBuffer));

		Converter->InputSample = InputSample;
		Converter->InputBuffer = InputBuffer;
	}

	// allocate output
	{
		IMFSample* OutputSample;
		IMFMediaBuffer* OutputBuffer;
		HR(MFCreateSample(&OutputSample));
		HR(MFCreateMemoryBuffer(BufferSampleCount * OutputFormat.nBlockAlign, &OutputBuffer));
		HR(IMFSample_AddBuffer(OutputSample, OutputBuffer));

		Converter->OutputSample = OutputSample;
		Converter->OutputBuffer = OutputBuffer;
	}

	Converter->InputSampleSize = InputFormat->nBlockAlign;
	Converter->InputSampleRate = InputFormat->nSamplesPerSec;
	HR(IMFTransform_ProcessMessage(Converter->Transform, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
}

void AudioConverter_Destroy(AudioConverter* Converter)
{
	IMFTransform_Release(Converter->Transform);
	IMFMediaBuffer_Release(Converter->OutputBuffer);
	IMFMediaBuffer_Release(Converter->InputBuffer);
	IMFSample_Release(Converter->OutputSample);
	IMFSample_Release(Converter->InputSample);
}

void AudioConverter_Flush(AudioConverter* Converter)
{
	HR(IMFTransform_ProcessMessage(Converter->Transform, MFT_MESSAGE_COMMAND_DRAIN, 0));
}

void AudioConverter_SendInput(AudioConverter* Converter, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount)
{
	BYTE* BufferData;
	DWORD MaxLength;
	HR(IMFMediaBuffer_Lock(Converter->InputBuffer, &BufferData, &MaxLength, NULL));

	uint32_t BufferSize = SampleCount * Converter->InputSampleSize;
	Assert(BufferSize <= MaxLength);

	if (Samples)
	{
		CopyMemory(BufferData, Samples, BufferSize);
	}
	else
	{
		ZeroMemory(BufferData, BufferSize);
	}

	HR(IMFMediaBuffer_Unlock(Converter->InputBuffer));
	HR(IMFMediaBuffer_SetCurrentLength(Converter->InputBuffer, BufferSize));

	// setup input time & duration
	HR(IMFSample_SetSampleDuration(Converter->InputSample, MFllMulDiv(SampleCount, MF_UNITS_PER_SECOND, Converter->InputSampleRate, 0)));
	HR(IMFSample_SetSampleTime(Converter->InputSample, MFllMulDiv(Time, MF_UNITS_PER_SECOND, TimePeriod, 0)));

	HR(IMFTransform_ProcessInput(Converter->Transform, 0, Converter->InputSample, 0));
}

IMFSample* AudioConverter_GetOutput(AudioConverter* Converter)
{
	DWORD Status;
	MFT_OUTPUT_DATA_BUFFER Output = { .pSample = Converter->OutputSample };
	HRESULT hr = IMFTransform_ProcessOutput(Converter->Transform, 0, 1, &Output, &Status);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
	{
		return NULL;
	}
	Assert(SUCCEEDED(hr));
	Assert(Output.pEvents == NULL);

	return Converter->OutputSample;
}
