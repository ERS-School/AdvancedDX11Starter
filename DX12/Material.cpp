#include "Material.h"

#include <string>
#include <stdio.h>

#include "DX12Helper.h"

Material::Material(Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState, DirectX::XMFLOAT3 _colorTint, DirectX::XMFLOAT2 _uvScale, DirectX::XMFLOAT2 _uvOffset)
    :
    pipelineState(_pipelineState),
    colorTint(_colorTint),
    uvOffset(_uvOffset),
	uvScale(_uvScale),
	finalized(false),
	highestSRVSlot(-1)
{
	ZeroMemory(textureSRVsBySlot, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) * 128);
	finalGPUHandleForSRVs = {};
    printf("ScaleX: %f, ScaleY: %f\n", _uvScale.x, _uvScale.y);
}
void Material::AddTexture(D3D12_CPU_DESCRIPTOR_HANDLE _srvHandle, int _slot)
{
    // First check if valid
    if (finalized || _slot < 0 || _slot >= 128)
        return;

    // Save. Check if this was the highest slot
    textureSRVsBySlot[_slot] = _srvHandle;
    highestSRVSlot = max(highestSRVSlot, _slot);
}
void Material::FinalizeMaterial()
{
    if (finalized)
        return;

    DX12Helper& dx12helper = DX12Helper::GetInstance();

    // Copy all SRVs into the shader-visible CBV/SRV heap
    // Get the handle that goes with the first texture
    for (int i = 0; i <= highestSRVSlot; i++)
    {
        // Copy a single SRV at a time
        // They're currently all in separate heaps
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12helper.CopySRVsToDescriptorHeapAndGetGPUDescriptorHandle(textureSRVsBySlot[i], 1);

        // Save the first handle
        if (i == 0)
            finalGPUHandleForSRVs = gpuHandle;
    }

    // Mark as done
    finalized = true;
}

#pragma region Getters
Microsoft::WRL::ComPtr<ID3D12PipelineState> Material::GetPipelineState()
{
    return pipelineState;
}

DirectX::XMFLOAT2 Material::GetUVScale()
{
    return uvScale;
}

DirectX::XMFLOAT2 Material::GetUVOffset()
{
    return uvOffset;
}

DirectX::XMFLOAT3 Material::GetColorTint()
{
    return colorTint;
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetFinalGPUHandleForTextures()
{
    return finalGPUHandleForSRVs;
}
#pragma endregion

#pragma region Setters
void Material::SetPipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState)
{
    pipelineState = _pipelineState;
}

void Material::SetUVScale(DirectX::XMFLOAT2 _scale)
{
    uvScale = _scale;
}

void Material::SetUVOffset(DirectX::XMFLOAT2 _offset)
{
    uvOffset = _offset;
}

void Material::SetColorTint(DirectX::XMFLOAT3 _tint)
{
    colorTint = _tint;
}
#pragma endregion