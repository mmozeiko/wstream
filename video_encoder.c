#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "video_encoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <strmif.h>
#include <mferror.h>
#include <codecapi.h>

#include <stddef.h>
#include <intrin.h>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "strmiids.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

#define MF_UNITS_PER_SECOND 10000000ULL

#define MF64(high, low) (((UINT64)high << 32) | (low))

// why this is not documented anywhere?
DEFINE_GUID(MF_XVP_PLAYBACK_MODE, 0x3c5d293f, 0xad67, 0x4e29, 0xaf, 0x12, 0xcf, 0x3e, 0x23, 0x8a, 0xcc, 0xe9);

static DWORD WINAPI VideoEncoder__Thread(LPVOID Arg)
{
	VideoEncoder* Encoder = Arg;

	IMFMediaEventGenerator* Generator;
	HR(IMFAttributes_QueryInterface(Encoder->Encoder, &IID_IMFMediaEventGenerator, &Generator));

	for (;;)
	{
		IMFMediaEvent* Event;
		HRESULT hr = IMFMediaEventGenerator_GetEvent(Generator, 0, &Event);
		if (hr == MF_E_SHUTDOWN)
		{
			break;
		}
		HR(hr);

		MediaEventType EventType;
		HR(IMFMediaEvent_GetType(Event, &EventType));
		IMFMediaEvent_Release(Event);

		if (EventType == METransformNeedInput)
		{
			HANDLE Events[] = { Encoder->InputQueued, Encoder->Stop };
			DWORD Wait = WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE);
			if (Wait == WAIT_OBJECT_0)
			{
				size_t Index = Encoder->InputQueuedIndex;
				Encoder->InputQueuedIndex = (Index + 1) % VIDEO_ENCODER_BUFFER_COUNT;

				IMFSample* Input = Encoder->EncoderInput[Index];
				hr = IMFTransform_ProcessInput(Encoder->Encoder, 0, Input, 0);
				IMFSample_Release(Input);
				Encoder->EncoderInput[Index] = NULL;

				if (hr == MF_E_NOTACCEPTING)
				{
					break;
				}
				HR(hr);

				ReleaseSemaphore(Encoder->InputFree, 1, NULL);
			}
		}
		else if (EventType == METransformHaveOutput)
		{
			DWORD Status;
			MFT_OUTPUT_DATA_BUFFER Output = { 0 };
			HR(IMFTransform_ProcessOutput(Encoder->Encoder, 0, 1, &Output, &Status));

			IMFMediaBuffer* Buffer;
			if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(Output.pSample, &Buffer)))
			{
				BYTE* Data;
				DWORD Size;
				if (SUCCEEDED(IMFMediaBuffer_Lock(Buffer, &Data, NULL, &Size)))
				{
					UINT32 Type;
					HR(IMFSample_GetUINT32(Output.pSample, &MFSampleExtension_VideoEncodePictureType, &Type));

					LONGLONG PresentTime;
					HR(IMFSample_GetSampleTime(Output.pSample, &PresentTime));

					LONGLONG DecodeTime;
					if (FAILED(IMFSample_GetUINT64(Output.pSample, &MFSampleExtension_DecodeTimestamp, &DecodeTime)))
					{
						DecodeTime = PresentTime;
					}

					Encoder->Callback(Encoder, DecodeTime, PresentTime, MF_UNITS_PER_SECOND, Type == eAVEncH264PictureType_IDR, Data, Size);

					HR(IMFMediaBuffer_Unlock(Buffer));
				}
				IMFMediaBuffer_Release(Buffer);
			}
			IMFSample_Release(Output.pSample);
		}
	}

	IMFMediaEventGenerator_Release(Generator);

	return 0;
}

void VideoEncoder_Init(VideoEncoder* Encoder, ID3D11Device* Device, const VideoEncoderConfig* Config, VideoEncoder_Callback* Callback)
{
	HR(MFStartup(MF_VERSION, MFSTARTUP_LITE));

	HR(CoCreateInstance(&CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (LPVOID*)&Encoder->Converter));

	MFT_REGISTER_TYPE_INFO Input = { .guidMajorType = MFMediaType_Video, .guidSubtype = MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO Output = { .guidMajorType = MFMediaType_Video, .guidSubtype = MFVideoFormat_H264 };

	IMFActivate** Activate;
	UINT32 ActivateCount;
	HR(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &Input, &Output, &Activate, &ActivateCount));
	Assert(ActivateCount != 0);
	HR(IMFActivate_ActivateObject(Activate[0], &IID_IMFTransform, (LPVOID*)&Encoder->Encoder));
	for (UINT32 i = 0; i < ActivateCount; i++)
	{
		IMFActivate_Release(Activate[i]);
	}
	CoTaskMemFree(Activate);

	// unlock async MFT
	{
		IMFAttributes* Attributes;
		HR(IMFTransform_GetAttributes(Encoder->Encoder, &Attributes));

		UINT32 Value;
		HR(IMFAttributes_GetUINT32(Attributes, &MF_TRANSFORM_ASYNC, &Value));
		Assert(Value);
		
		HR(IMFAttributes_SetUINT32(Attributes, &MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
		IMFAttributes_Release(Attributes);
	}

	// setup converter & encoder with D3D11 device
	{
		UINT Token;
		IMFDXGIDeviceManager* Manager;
		HR(MFCreateDXGIDeviceManager(&Token, &Manager));
		HR(IMFDXGIDeviceManager_ResetDevice(Manager, (IUnknown*)Device, Token));
		HR(IMFTransform_ProcessMessage(Encoder->Converter, MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)Manager));
		HR(IMFTransform_ProcessMessage(Encoder->Encoder, MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)Manager));
		IMFDXGIDeviceManager_Release(Manager);
	}

	// inform converter that we will be providing output storage
	{
		IMFAttributes* Attributes;
		HR(IMFTransform_GetAttributes(Encoder->Converter, &Attributes));
		HR(IMFAttributes_SetUINT32(Attributes, &MF_XVP_PLAYBACK_MODE, TRUE));
		HR(IMFAttributes_SetUINT32(Attributes, &MF_XVP_CALLER_ALLOCATES_OUTPUT, TRUE));
		IMFAttributes_Release(Attributes);
	}

	// setup H264 encoder
	{
		DWORD KeyframeInterval = 2; // seconds

		ICodecAPI* Codec;
		HR(IMFTransform_QueryInterface(Encoder->Encoder, &IID_ICodecAPI, &Codec));

		// CBR rate control
		{
			VARIANT RateControl;
			RateControl.vt = VT_UI4;
			RateControl.ulVal = eAVEncCommonRateControlMode_CBR;
			HR(ICodecAPI_SetValue(Codec, &CODECAPI_AVEncCommonRateControlMode, &RateControl));
		}

		// bitrate
		{
			VARIANT Bitrate;
			Bitrate.vt = VT_UI4;
			Bitrate.ulVal = Config->Bitrate * 1000;
			HR(ICodecAPI_SetValue(Codec, &CODECAPI_AVEncCommonMeanBitRate, &Bitrate));

			VARIANT BufferSize;
			BufferSize.vt = VT_UI4;
			BufferSize.ulVal = Config->Bitrate * 1000 * KeyframeInterval / 8;
			HR(ICodecAPI_SetValue(Codec, &CODECAPI_AVEncCommonBufferSize, &BufferSize));
		}

		// keyframe interval
		{
			VARIANT Gop;
			Gop.vt = VT_UI4;
			Gop.ulVal = KeyframeInterval * Config->FramerateNum / Config->FramerateDen;
			HR(ICodecAPI_SetValue(Codec, &CODECAPI_AVEncMPVGOPSize, &Gop));
		}

		// enable 2 B-frames if possible
		{
			VARIANT BFrames;
			BFrames.vt = VT_UI4;
			BFrames.ulVal = 2;
			ICodecAPI_SetValue(Codec, &CODECAPI_AVEncMPVDefaultBPictureCount, &BFrames);
		}

		// enable CABAC if possible
		{
			VARIANT Cabac;
			Cabac.vt = VT_BOOL;
			Cabac.boolVal = VARIANT_TRUE;
			ICodecAPI_SetValue(Codec, &CODECAPI_AVEncH264CABACEnable, &Cabac);
		}

		// enable low latency mode if possible
		{
			VARIANT LowLatency;
			LowLatency.vt = VT_BOOL;
			LowLatency.boolVal = VARIANT_TRUE;
			ICodecAPI_SetValue(Codec, &CODECAPI_AVLowLatencyMode, &LowLatency);
		}

		ICodecAPI_Release(Codec);
	}

	// create video input/output types
	{
		IMFMediaType* ConverterInput;
		HR(MFCreateMediaType(&ConverterInput));
		HR(IMFMediaType_SetGUID(ConverterInput, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
		HR(IMFMediaType_SetGUID(ConverterInput, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32));
		HR(IMFMediaType_SetUINT32(ConverterInput, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		HR(IMFMediaType_SetUINT64(ConverterInput, &MF_MT_FRAME_SIZE, MF64(Config->InputWidth, Config->InputHeight)));

		IMFMediaType* EncoderInput;
		HR(MFCreateMediaType(&EncoderInput));
		HR(IMFMediaType_SetGUID(EncoderInput, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
		HR(IMFMediaType_SetGUID(EncoderInput, &MF_MT_SUBTYPE, &MFVideoFormat_NV12));
		HR(IMFMediaType_SetUINT32(EncoderInput, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		HR(IMFMediaType_SetUINT32(EncoderInput, &MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
		HR(IMFMediaType_SetUINT32(EncoderInput, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
		HR(IMFMediaType_SetUINT32(EncoderInput, &MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709));
		HR(IMFMediaType_SetUINT32(EncoderInput, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
		HR(IMFMediaType_SetUINT64(EncoderInput, &MF_MT_FRAME_RATE, MF64(Config->FramerateNum, Config->FramerateDen)));
		HR(IMFMediaType_SetUINT64(EncoderInput, &MF_MT_FRAME_SIZE, MF64(Config->OutputWidth, Config->OutputHeight)));

		IMFMediaType* EncoderOutput;
		HR(MFCreateMediaType(&EncoderOutput));
		HR(IMFMediaType_SetGUID(EncoderOutput, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
		HR(IMFMediaType_SetGUID(EncoderOutput, &MF_MT_SUBTYPE, &MFVideoFormat_H264));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
		HR(IMFMediaType_SetUINT64(EncoderOutput, &MF_MT_FRAME_RATE, MF64(Config->FramerateNum, Config->FramerateDen)));
		HR(IMFMediaType_SetUINT64(EncoderOutput, &MF_MT_FRAME_SIZE, MF64(Config->OutputWidth, Config->OutputHeight)));
		HR(IMFMediaType_SetUINT32(EncoderOutput, &MF_MT_AVG_BITRATE, Config->Bitrate * 1000));

		// setup video converter & encoder types
		HR(IMFTransform_SetOutputType(Encoder->Converter, 0, EncoderInput, 0));
		HR(IMFTransform_SetInputType(Encoder->Converter, 0, ConverterInput, 0));

		HR(IMFTransform_SetOutputType(Encoder->Encoder, 0, EncoderOutput, 0));
		HR(IMFTransform_SetInputType(Encoder->Encoder, 0, EncoderInput, 0));

		IMFMediaType_Release(ConverterInput);
		IMFMediaType_Release(EncoderInput);
		IMFMediaType_Release(EncoderOutput);
	}

	// check that video converter & encoder allows to pass our allocated buffers
	//DWORD ConverterInputSize;
	//DWORD EncoderInputSize;
	{
		MFT_OUTPUT_STREAM_INFO Info;
		HR(IMFTransform_GetOutputStreamInfo(Encoder->Converter, 0, &Info));
		Assert((Info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == MFT_OUTPUT_STREAM_PROVIDES_SAMPLES);
		//Assert(Info.cbAlignment <= 1);

		HR(IMFTransform_GetOutputStreamInfo(Encoder->Encoder, 0, &Info));
		Assert((Info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == MFT_OUTPUT_STREAM_PROVIDES_SAMPLES);

		//HR(MFCalculateImageSize(&MFVideoFormat_RGB32, Config->Width, Config->Height, &ConverterInputSize));
		//HR(MFCalculateImageSize(&MFVideoFormat_NV12, Config->Width, Config->Height, &EncoderInputSize));
	}

	// allocate RGB input for converter
	{
		D3D11_TEXTURE2D_DESC Desc =
		{
			.Width = Config->InputWidth,
			.Height = Config->InputHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_UNORDERED_ACCESS,
		};

		ID3D11Texture2D* InputTexture;
		HR(ID3D11Device_CreateTexture2D(Device, &Desc, NULL, &InputTexture));

		IMFSample* InputSample;
		HR(MFCreateSample(&InputSample));
		
		// duration and timestamp doesn't need to be correct, but Video Converter MFT does not like if they are not set, or contain bad values
		HR(IMFSample_SetSampleDuration(InputSample, MFllMulDiv(Encoder->FramerateNum, MF_UNITS_PER_SECOND, Encoder->FramerateDen, 0)));
		HR(IMFSample_SetSampleTime(InputSample, 0));

		IMFMediaBuffer* InputBuffer;
		HR(MFCreateDXGISurfaceBuffer(&IID_ID3D11Texture2D, (IUnknown*)InputTexture, 0, FALSE, &InputBuffer));
		HR(IMFSample_AddBuffer(InputSample, InputBuffer));
		IMFMediaBuffer_Release(InputBuffer);

		Encoder->InputTexture = InputTexture;
		Encoder->InputSample = InputSample;
	}

	// allocate YUV output for converter & input to encoder
	{
		for (UINT i = 0; i < VIDEO_ENCODER_BUFFER_COUNT; i++)
		{
			D3D11_TEXTURE2D_DESC Desc =
			{
				.Width = Config->OutputWidth,
				.Height = Config->OutputHeight,
				.MipLevels = 1,
				.ArraySize = 1,
				.Format = DXGI_FORMAT_NV12,
				.SampleDesc = { 1, 0 },
				.Usage = D3D11_USAGE_DEFAULT,
				.BindFlags = D3D11_BIND_RENDER_TARGET,
			};

			ID3D11Texture2D* Texture;
			HR(ID3D11Device_CreateTexture2D(Device, &Desc, NULL, &Texture));

			IMFMediaBuffer* Buffer;
			HR(MFCreateDXGISurfaceBuffer(&IID_ID3D11Texture2D, (IUnknown*)Texture, 0, FALSE, &Buffer));
			ID3D11Texture2D_Release(Texture);

			IMFSample* Sample;
			HR(MFCreateSample(&Sample));
			HR(IMFSample_AddBuffer(Sample, Buffer));
			IMFMediaBuffer_Release(Buffer);

			Encoder->ConvertedSample[i] = Sample;
		}
	}

	ID3D11Device_GetImmediateContext(Device, &Encoder->Context);

	HR(IMFTransform_ProcessMessage(Encoder->Converter, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
	HR(IMFTransform_ProcessMessage(Encoder->Encoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

	Encoder->InputFree = CreateSemaphoreW(NULL, VIDEO_ENCODER_BUFFER_COUNT, VIDEO_ENCODER_BUFFER_COUNT, NULL);;
	Encoder->InputQueued = CreateSemaphoreW(NULL, 0, VIDEO_ENCODER_BUFFER_COUNT, NULL);
	Encoder->InputFreeIndex = 0;
	Encoder->InputQueuedIndex = 0;

	Encoder->Stopping = false;
	Encoder->InputWidth = Config->InputWidth;
	Encoder->InputHeight = Config->InputHeight;
	Encoder->FramerateNum = Config->FramerateNum;
	Encoder->FramerateDen = Config->FramerateDen;

	Encoder->Callback = Callback;

	Encoder->Stop = CreateEventW(NULL, FALSE, FALSE, NULL);
	Assert(Encoder->Stop);

	Encoder->Thread = CreateThread(NULL, 0, &VideoEncoder__Thread, Encoder, 0, NULL);
	Assert(Encoder->Thread);
}

void VideoEncoder_Done(VideoEncoder* Encoder)
{
	SetEvent(Encoder->Stop);

	IMFShutdown* Shutdown;
	if (SUCCEEDED(IMFTransform_QueryInterface(Encoder->Encoder, &IID_IMFShutdown, &Shutdown)))
	{
		HR(IMFShutdown_Shutdown(Shutdown));
		IMFShutdown_Release(Shutdown);
	}

	WaitForSingleObject(Encoder->Thread, INFINITE);
	CloseHandle(Encoder->Thread);
	CloseHandle(Encoder->Stop);
	CloseHandle(Encoder->InputFree);
	CloseHandle(Encoder->InputQueued);

	IMFTransform_Release(Encoder->Encoder);
	IMFTransform_Release(Encoder->Converter);

	ID3D11DeviceContext_Release(Encoder->Context);

	HR(MFShutdown());
}

uint32_t VideoEncoder_GetHeader(VideoEncoder* Encoder, uint8_t* Header, uint32_t MaxSize)
{
	IMFMediaType* OutputType;
	HR(IMFTransform_GetOutputCurrentType(Encoder->Encoder, 0, &OutputType));

	UINT32 HeaderSize;
	HR(IMFMediaType_GetBlobSize(OutputType, &MF_MT_MPEG_SEQUENCE_HEADER, &HeaderSize));
	if (HeaderSize <= MaxSize)
	{
		HR(IMFMediaType_GetBlob(OutputType, &MF_MT_MPEG_SEQUENCE_HEADER, Header, MaxSize, &HeaderSize));
	}
	IMFMediaType_Release(OutputType);

	return HeaderSize;
}

bool VideoEncoder_Encode(VideoEncoder* Encoder, uint64_t Time, uint64_t TimePeriod, const RECT* Rect, ID3D11Texture2D* Texture)
{
	if (WaitForSingleObject(Encoder->InputFree, 0) != WAIT_OBJECT_0)
	{
		// too many frames already queued up, encoder probably cannot keep up => dropping frame
		return false;
	}

	size_t Index = Encoder->InputFreeIndex;
	Encoder->InputFreeIndex = (Index + 1) % VIDEO_ENCODER_BUFFER_COUNT;

	// copy data from input texture
	{
		D3D11_BOX Box;
		if (Rect)
		{
			Box.left = (UINT)Rect->left;
			Box.top = (UINT)Rect->top;
			Box.right = (UINT)Rect->right;
			Box.bottom = (UINT)Rect->bottom;
		}
		else
		{
			Box.top = 0;
			Box.bottom = 0;
			Box.right = Encoder->InputWidth;
			Box.bottom = Encoder->InputHeight;
		}

		Box.front = 0;
		Box.back = 1;

		ID3D11DeviceContext_CopySubresourceRegion(Encoder->Context, (ID3D11Resource*)Encoder->InputTexture, 0, 0, 0, 0, (ID3D11Resource*)Texture, 0, &Box);
	}

	// send RGB input to converter
	HR(IMFTransform_ProcessInput(Encoder->Converter, 0, Encoder->InputSample, 0));

	// get YUV output from converter
	{
		IMFSample* Converted = Encoder->ConvertedSample[Index];
		MFT_OUTPUT_DATA_BUFFER Output = { .pSample = Converted };

		DWORD Status;
		HR(IMFTransform_ProcessOutput(Encoder->Converter, 0, 1, &Output, &Status));

		IMFSample* Input = Encoder->EncoderInput[Index];

		HR(IMFSample_SetSampleDuration(Converted, MFllMulDiv(Encoder->FramerateNum, MF_UNITS_PER_SECOND, Encoder->FramerateDen, 0)));
		HR(IMFSample_SetSampleTime(Converted, MFllMulDiv(Time, MF_UNITS_PER_SECOND, TimePeriod, 0)));

		Encoder->EncoderInput[Index] = Converted;
		IMFSample_AddRef(Encoder->EncoderInput[Index]);
	}

	// allow background thread to use YUV input
	ReleaseSemaphore(Encoder->InputQueued, 1, NULL);

	return true;
}
