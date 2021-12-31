#pragma once

#include <windows.h>
#include <d3d11.h>

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// private
	uint32_t InputWidth;
	uint32_t InputHeight;
	uint32_t OutputWidth;
	uint32_t OutputHeight;
	ID3D11DeviceContext* Context;
	ID3D11ComputeShader* ResizeShader;
	ID3D11ComputeShader* ConvertShader;
	// BGR input texture
	ID3D11Texture2D* InputTexture;
	ID3D11RenderTargetView* InputRenderTarget;
	ID3D11ShaderResourceView* ResizeInputView;
	// BGR resized texture
	ID3D11Texture2D* ResizedTexture;
	ID3D11ShaderResourceView* ConvertInputView;
	ID3D11UnorderedAccessView* ResizeOutputView;
} VideoConverter;

// VideoConverter are two compute shaders, that
// 1) optionally resizes BGRA texture
// 2) then converts BGRA to NV12 texture

void VideoConverter_Create(VideoConverter* Converter, ID3D11Device* Device, uint32_t InputWidth, uint32_t InputHeight, uint32_t OutputWidth, uint32_t OutputHeight);
void VideoConverter_Destroy(VideoConverter* Converter);

// OutputViews is [0] for Y and [1] for UV
void VideoConverter_Convert(VideoConverter* Converter, const RECT* Rect, ID3D11Texture2D* Texture, ID3D11UnorderedAccessView* OutputViews[2]);

// helper function to allocate NV12 texture & output views
void VideoConverter_CreateOutput(VideoConverter* Converter, ID3D11Device* Device, ID3D11Texture2D** Texture, ID3D11UnorderedAccessView* OutputViews[2]);
