#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>
#include <unordered_map>

#include "Camera.h"
#include "Transform.h"

class Material
{
public:
	Material(Microsoft::WRL::ComPtr < ID3D12PipelineState> _pipelineState,
				DirectX::XMFLOAT3 _colorTint,
				DirectX::XMFLOAT2 _uvScale = DirectX::XMFLOAT2(1, 1),
				DirectX::XMFLOAT2 _uvOffset = DirectX::XMFLOAT2(0, 0));
	void AddTexture(D3D12_CPU_DESCRIPTOR_HANDLE _srv, int _slot);
	void FinalizeMaterial();

	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState();
	DirectX::XMFLOAT2 GetUVScale();
	DirectX::XMFLOAT2 GetUVOffset();
	DirectX::XMFLOAT3 GetColorTint();
	D3D12_GPU_DESCRIPTOR_HANDLE GetFinalGPUHandleForTextures();

	void SetPipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState);
	void SetUVScale(DirectX::XMFLOAT2 _scale);
	void SetUVOffset(DirectX::XMFLOAT2 _offset);
	void SetColorTint(DirectX::XMFLOAT3 _tint);

private:
	// Pipeline state. Can be shared b/t materials
	// (Includes shaders)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	// Material properties
	DirectX::XMFLOAT3 colorTint;
	DirectX::XMFLOAT2 uvOffset;
	DirectX::XMFLOAT2 uvScale;

	// Texture-related GPU tracking
	bool finalized;
	int highestSRVSlot;
	D3D12_CPU_DESCRIPTOR_HANDLE textureSRVsBySlot[128];
	D3D12_GPU_DESCRIPTOR_HANDLE finalGPUHandleForSRVs;
};
