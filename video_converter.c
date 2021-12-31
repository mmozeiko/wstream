#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>

#include "video_converter.h"
#include "video_converter_resize_shader.h"
#include "video_converter_convert_shader.h"

#include <dwmapi.h>

#pragma comment (lib, "dwmapi.lib")

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif

#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

void VideoConverter_Create(VideoConverter* Converter, ID3D11Device* Device, uint32_t InputWidth, uint32_t InputHeight, uint32_t OutputWidth, uint32_t OutputHeight)
{
	// input texture
	{
		D3D11_TEXTURE2D_DESC TextureDesc =
		{
			.Width = InputWidth,
			.Height = InputHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
		};
		HR(ID3D11Device_CreateTexture2D(Device, &TextureDesc, NULL, &Converter->InputTexture));
		HR(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource*)Converter->InputTexture, NULL, &Converter->InputRenderTarget));
	}

	// resized texture
	if (InputWidth == OutputWidth && InputHeight == OutputHeight)
	{
		// no resize needed, InputTexture is used in Convert shader
		HR(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource*)Converter->InputTexture, NULL, &Converter->ConvertInputView));
		Converter->ResizeShader = NULL;
	}
	else
	{
		// resize needed, InputTexture is used in Resize Shader
		HR(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource*)Converter->InputTexture, NULL, &Converter->ResizeInputView));

		D3D11_TEXTURE2D_DESC TextureDesc =
		{
			.Width = OutputWidth,
			.Height = OutputHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
		};
		HR(ID3D11Device_CreateTexture2D(Device, &TextureDesc, NULL, &Converter->ResizedTexture));

		D3D11_SHADER_RESOURCE_VIEW_DESC ResourceView =
		{
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D.MipLevels = 1,
			.Texture2D.MostDetailedMip = 0,
		};
		HR(ID3D11Device_CreateShaderResourceView(Device, (ID3D11Resource*)Converter->ResizedTexture, &ResourceView, &Converter->ConvertInputView));

		// because D3D 11.0 does not support B8G8R8A8_UNORM for UAV, create uint UAV used on BGRA texture
		D3D11_UNORDERED_ACCESS_VIEW_DESC AccessViewDesc =
		{
			.Format = DXGI_FORMAT_R32_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D.MipSlice = 0,
		};
		HR(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource*)Converter->ResizedTexture, &AccessViewDesc, &Converter->ResizeOutputView));

		HR(ID3D11Device_CreateComputeShader(Device, ResizeShaderBytes, sizeof(ResizeShaderBytes), NULL, &Converter->ResizeShader));
	}

	HR(ID3D11Device_CreateComputeShader(Device, ConvertShaderBytes, sizeof(ConvertShaderBytes), NULL, &Converter->ConvertShader));

	ID3D11Device_GetImmediateContext(Device, &Converter->Context);
	Converter->InputWidth = InputWidth;
	Converter->InputHeight = InputHeight;
	Converter->OutputWidth = OutputWidth;
	Converter->OutputHeight = OutputHeight;
}

void VideoConverter_Destroy(VideoConverter* Converter)
{
	ID3D11Texture2D_Release(Converter->InputTexture);
	ID3D11RenderTargetView_Release(Converter->InputRenderTarget);

	if (Converter->ResizeShader)
	{
		ID3D11Texture2D_Release(Converter->ResizedTexture);
		ID3D11ShaderResourceView_Release(Converter->ResizeInputView);
		ID3D11UnorderedAccessView_Release(Converter->ResizeOutputView);
		ID3D11ComputeShader_Release(Converter->ResizeShader);
	}

	ID3D11ShaderResourceView_Release(Converter->ConvertInputView);
	ID3D11ComputeShader_Release(Converter->ConvertShader);

	ID3D11DeviceContext_Release(Converter->Context);
}

void VideoConverter_Convert(VideoConverter* Converter, const RECT* Rect, ID3D11Texture2D* Texture, ID3D11UnorderedAccessView* OutputViews[2])
{
	// copy to input texture
	{
		D3D11_BOX Box =
		{
			.left = Rect ? Rect->left : 0,
			.top = Rect ? Rect->top : 0,
			.right = Rect ? Rect->right : Converter->InputWidth,
			.bottom = Rect ? Rect->bottom : Converter->InputHeight,
			.front = 0,
			.back = 1,
		};

		DWORD Width = Box.right - Box.left;
		DWORD Height = Box.bottom - Box.top;
		if (Width < Converter->InputWidth || Height < Converter->InputHeight)
		{
			// TODO: more intelligent clearing to black, no need to clear everything, just extra border on right/bottom
			FLOAT Black[] = { 0, 0, 0, 0 };
			ID3D11DeviceContext_ClearRenderTargetView(Converter->Context, Converter->InputRenderTarget, Black);

			Box.right = Box.left + min(Converter->InputWidth, Box.right);
			Box.bottom = Box.top + min(Converter->InputHeight, Box.bottom);
		}
		ID3D11DeviceContext_CopySubresourceRegion(Converter->Context, (ID3D11Resource*)Converter->InputTexture, 0, 0, 0, 0, (ID3D11Resource*)Texture, 0, &Box);
	}

	// resize if needed
	if (Converter->ResizeShader != NULL)
	{
		ID3D11DeviceContext_CSSetShader(Converter->Context, Converter->ResizeShader, NULL, 0);

		// must bind input first, because otherwise ConvertInputView on input will reference same texture as ResizeOutputView on output from previous frame
		ID3D11DeviceContext_CSSetShaderResources(Converter->Context, 0, 1, &Converter->ResizeInputView);
		ID3D11DeviceContext_CSSetUnorderedAccessViews(Converter->Context, 0, 1, &Converter->ResizeOutputView, NULL);

		// TODO: compare performance with two-pass separable filter
		ID3D11DeviceContext_Dispatch(Converter->Context, (Converter->OutputWidth + 15) / 16, (Converter->OutputHeight + 15) / 16, 1);
	}

	// convert to NV12
	{
		ID3D11DeviceContext_CSSetShader(Converter->Context, Converter->ConvertShader, NULL, 0);

		// must bind output first, because otherwise ConvertInputView on input will reference same texture as ResizeOutputView on output from resize shader above
		ID3D11DeviceContext_CSSetUnorderedAccessViews(Converter->Context, 0, 2, OutputViews, NULL);
		ID3D11DeviceContext_CSSetShaderResources(Converter->Context, 0, 1, &Converter->ConvertInputView);

		ID3D11DeviceContext_Dispatch(Converter->Context, (Converter->OutputWidth / 2 + 15) / 16, (Converter->OutputHeight / 2 + 15) / 16, 1);
	}
}

void VideoConverter_CreateOutput(VideoConverter* Converter, ID3D11Device* Device, ID3D11Texture2D** Texture, ID3D11UnorderedAccessView* OutputViews[2])
{
	D3D11_TEXTURE2D_DESC TextureDesc =
	{
		.Width = Converter->OutputWidth,
		.Height = Converter->OutputHeight,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_NV12,
		.SampleDesc = { 1, 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_UNORDERED_ACCESS,
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC ViewY =
	{
		.Format = DXGI_FORMAT_R8_UINT,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC ViewUV =
	{
		.Format = DXGI_FORMAT_R8G8_UINT,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
	};

	ID3D11Texture2D* TextureUV;
	HR(ID3D11Device_CreateTexture2D(Device, &TextureDesc, NULL, &TextureUV));
	HR(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource*)TextureUV, &ViewY, &OutputViews[0]));
	HR(ID3D11Device_CreateUnorderedAccessView(Device, (ID3D11Resource*)TextureUV, &ViewUV, &OutputViews[1]));
	*Texture = TextureUV;
}
