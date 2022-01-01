#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "audio_encoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>

#include <intrin.h>

#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "wmcodecdspuuid.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define MF_UNITS_PER_SECOND 10000000ULL

// MP3  https://docs.microsoft.com/en-us/windows/win32/medfound/mp3-audio-encoder
// AAC  https://docs.microsoft.com/en-us/windows/win32/medfound/aac-encoder
// AC3  https://docs.microsoft.com/en-us/windows/win32/medfound/dolby-digital-audio-encoder
// FLAC https://alax.info/blog/1839

void AudioEncoder_Create(AudioEncoder* Encoder, const AudioEncoderConfig* Config)
{
	HR(MFStartup(MF_VERSION, MFSTARTUP_LITE));
	AudioConverter_Create(&Encoder->Converter, Config->InputFormat, Config->OutputSampleRate, Config->OutputChannels, Config->InputFormat->nSamplesPerSec);

	const GUID* CodecClass;
	const GUID* OutputSubtype;

	switch (Config->Codec)
	{
	case AUDIO_ENCODER_WAV:
		CodecClass = NULL;
		break;
	case AUDIO_ENCODER_MP3:
		CodecClass = &CLSID_MP3ACMCodecWrapper;
		OutputSubtype = &MFAudioFormat_MP3;
		break;
	case AUDIO_ENCODER_AAC:
		CodecClass = &CLSID_AACMFTEncoder;
		OutputSubtype = &MFAudioFormat_AAC;
		break;
	case AUDIO_ENCODER_AC3:
		CodecClass = &CLSID_CMSDolbyDigitalEncMFT;
		OutputSubtype = &MFAudioFormat_Dolby_AC3;
		break;
	case AUDIO_ENCODER_FLAC:
		CodecClass = &CLSID_CMSFLACEncMFT;
		OutputSubtype = &MFAudioFormat_FLAC;
		break;
	default:
		Assert(false);
	}

	WAVEFORMATEX OutputFormat =
	{
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = (WORD)Config->OutputChannels,
		.nSamplesPerSec = Config->OutputSampleRate,
		.wBitsPerSample = sizeof(short) * 8,
	};
	OutputFormat.nBlockAlign = OutputFormat.nChannels * OutputFormat.wBitsPerSample / 8;
	OutputFormat.nAvgBytesPerSec = OutputFormat.nSamplesPerSec * OutputFormat.nBlockAlign;

	uint32_t EncoderOutputSize = 0;

	if (CodecClass)
	{
		HR(CoCreateInstance(CodecClass, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, &Encoder->Transform));

		// set output type
		{
			IMFMediaType* OutputType;
			HR(MFCreateMediaType(&OutputType));

			HR(IMFMediaType_SetGUID(OutputType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio));
			HR(IMFMediaType_SetGUID(OutputType, &MF_MT_SUBTYPE, OutputSubtype));
			HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_SAMPLES_PER_SECOND, Config->OutputSampleRate));
			HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_NUM_CHANNELS, Config->OutputChannels));

			if (Config->Codec == AUDIO_ENCODER_MP3)
			{
				MPEGLAYER3WAVEFORMAT Format =
				{
					.wID = MPEGLAYER3_ID_MPEG,
					.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF,
					.nBlockSize = (WORD)(144 * Config->BitrateKbits * 1000 / OutputFormat.nSamplesPerSec),
					.nFramesPerBlock = 1,
					.nCodecDelay = 0,
				};
				HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 1));
				HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, Config->BitrateKbits * 1000 / 8));
				HR(IMFMediaType_SetBlob(OutputType, &MF_MT_USER_DATA, (void*)(&Format.wfx + 1), MPEGLAYER3_WFX_EXTRA_BYTES));
			}
			else
			{
				HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
				if (Config->BitrateKbits)
				{
					HR(IMFMediaType_SetUINT32(OutputType, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, Config->BitrateKbits * 1000 / 8));
				}
			}
			HR(IMFTransform_SetOutputType(Encoder->Transform, 0, OutputType, 0));
			IMFMediaType_Release(OutputType);
		}

		// set input type
		{
			IMFMediaType* InputType;
			HR(MFCreateMediaType(&InputType));
			HR(MFInitMediaTypeFromWaveFormatEx(InputType, &OutputFormat, sizeof(OutputFormat)));
			HR(IMFTransform_SetInputType(Encoder->Transform, 0, InputType, 0));
			IMFMediaType_Release(InputType);
		}

		// verify input/ouput buffer properties
		{
			MFT_INPUT_STREAM_INFO InputInfo;
			HR(IMFTransform_GetInputStreamInfo(Encoder->Transform, 0, &InputInfo));
			Assert(InputInfo.cbAlignment <= 1);

			MFT_OUTPUT_STREAM_INFO OutputInfo;
			HR(IMFTransform_GetOutputStreamInfo(Encoder->Transform, 0, &OutputInfo));
			Assert((OutputInfo.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) == 0);
			Assert(OutputInfo.cbAlignment <= 1);
			EncoderOutputSize = OutputInfo.cbSize;
		}

		// allocate input
		{
			IMFSample* InputSample;
			IMFMediaBuffer* InputBuffer;
			HR(MFCreateSample(&InputSample));
			HR(MFCreateMemoryBuffer(OutputFormat.nSamplesPerSec * OutputFormat.nBlockAlign, &InputBuffer));
			HR(IMFSample_AddBuffer(InputSample, InputBuffer));

			Encoder->InputSample = InputSample;
			Encoder->InputBuffer = InputBuffer;
		}

		HR(IMFTransform_ProcessMessage(Encoder->Transform, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
	}
	else
	{
		Encoder->Transform = NULL;
	}

	if (EncoderOutputSize == 0)
	{
		EncoderOutputSize = Config->OutputSampleRate * OutputFormat.nBlockAlign;
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
}

void AudioEncoder_Destroy(AudioEncoder* Encoder)
{
	if (Encoder->Transform)
	{
		IMFMediaBuffer_Release(Encoder->InputBuffer);
		IMFSample_Release(Encoder->InputSample);
		IMFTransform_Release(Encoder->Transform);
	}

	IMFMediaBuffer_Release(Encoder->OutputBuffer);
	IMFSample_Release(Encoder->OutputSample);

	AudioConverter_Destroy(&Encoder->Converter);
	HR(MFShutdown());
}

uint32_t AudioEncoder_GetHeader(AudioEncoder* Encoder, uint8_t* Header, uint32_t MaxSize)
{
	IMFMediaType* OutputType;
	HR(IMFTransform_GetOutputCurrentType(Encoder->Transform, 0, &OutputType));

	UINT32 HeaderSize;
	if (FAILED(IMFMediaType_GetBlobSize(OutputType, &MF_MT_USER_DATA, &HeaderSize)))
	{
		return 0;
	}

	if (HeaderSize <= MaxSize)
	{
		HR(IMFMediaType_GetBlob(OutputType, &MF_MT_USER_DATA, Header, MaxSize, &HeaderSize));

		GUID Subtype;
		HR(IMFMediaType_GetGUID(OutputType, &MF_MT_SUBTYPE, &Subtype));

		if (IsEqualGUID(&Subtype, &MFAudioFormat_AAC))
		{
			// see HEAACWAVEFORMAT docs for description of what Header contains for AAC
			// MF_MT_USER_DATA returns only bytes after wfInfo.wfx member
			uint32_t SkipBytes = sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX);
			MoveMemory(Header, Header + SkipBytes, HeaderSize - SkipBytes);
			HeaderSize -= SkipBytes;
		}
	}

	IMFMediaType_Release(OutputType);
	return HeaderSize;
}

void AudioEncoder_ProcessInput(AudioEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const void* Samples, uint32_t SampleCount)
{
	AudioConverter_ProcessInput(&Encoder->Converter, Time, TimePeriod, Samples, SampleCount);
}

bool AudioEncoder_GetOutput(AudioEncoder* Encoder, AudioEncoderOutput* Output)
{
	if (Encoder->Transform)
	{
		for (;;)
		{
			// try to get encoded output
			DWORD Status;
			MFT_OUTPUT_DATA_BUFFER Out = { .pSample = Encoder->OutputSample };
			HRESULT hr = IMFTransform_ProcessOutput(Encoder->Transform, 0, 1, &Out, &Status);
			if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
			{
				// encoder needs more input, try to get output of converter to encode
				if (AudioConverter_GetOutput(&Encoder->Converter, Encoder->InputSample))
				{
					// encode newly converted output
					HR(IMFTransform_ProcessInput(Encoder->Transform, 0, Encoder->InputSample, 0));
					// try to get encoded output again
					continue;
				}
				else
				{
					// not enough output from converter
					return false;
				}
			}
			Assert(SUCCEEDED(hr));

			BYTE* BufferData;
			DWORD BufferSize;
			HR(IMFMediaBuffer_Lock(Encoder->OutputBuffer, &BufferData, NULL, &BufferSize));

			LONGLONG Time;
			HR(IMFSample_GetSampleTime(Encoder->OutputSample, &Time));

			*Output = (AudioEncoderOutput)
			{
				.Data = BufferData,
				.Size = BufferSize,
				.Time = Time,
				.TimePeriod = MF_UNITS_PER_SECOND,
			};

			return true;
		}
	}
	else // in case of WAV codec
	{
		// try to get output of converter
		if (AudioConverter_GetOutput(&Encoder->Converter, Encoder->OutputSample))
		{
			BYTE* BufferData;
			DWORD BufferSize;
			HR(IMFMediaBuffer_Lock(Encoder->OutputBuffer, &BufferData, NULL, &BufferSize));

			LONGLONG Time;
			HR(IMFSample_GetSampleTime(Encoder->OutputSample, &Time));

			*Output = (AudioEncoderOutput)
			{
				.Data = BufferData,
				.Size = BufferSize,
				.Time = Time,
				.TimePeriod = MF_UNITS_PER_SECOND,
			};

			return true;
		}
		else
		{
			// not enough output from converter
			return false;
		}
	}
}

void AudioEncoder_ReleaseOutput(AudioEncoder* Encoder)
{
	HR(IMFMediaBuffer_Unlock(Encoder->OutputBuffer));
}
