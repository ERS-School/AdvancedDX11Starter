#pragma once

#include <memory>

#include "Mesh.h"
#include "SimpleShader.h"
#include "Camera.h"

#include <wrl/client.h> // Used for ComPtr

class Sky
{
public:

	// Constructor that loads a DDS cube map file
	Sky(
		const wchar_t* cubemapDDSFile, 
		std::shared_ptr<Mesh> mesh,
		std::shared_ptr<SimpleVertexShader> skyVS,
		std::shared_ptr<SimplePixelShader> skyPS,
		Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerOptions, 	
		Microsoft::WRL::ComPtr<ID3D11Device> device,
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context
	);

	// Constructor that loads 6 textures and makes a cube map
	Sky(
		const wchar_t* right,
		const wchar_t* left,
		const wchar_t* up,
		const wchar_t* down,
		const wchar_t* front,
		const wchar_t* back,
		std::shared_ptr<Mesh> mesh,
		std::shared_ptr<SimpleVertexShader> skyVS,
		std::shared_ptr<SimplePixelShader> skyPS,
		Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerOptions,
		Microsoft::WRL::ComPtr<ID3D11Device> device,
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context,
		std::shared_ptr<SimpleVertexShader> _fullscreenVS,
		std::shared_ptr<SimplePixelShader> _iblIrradMapPS,
		std::shared_ptr<SimplePixelShader> _iblSpecConvPS,
		std::shared_ptr<SimplePixelShader> _iblBRDFLookUpPS
	);

	~Sky();

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetIBLIrradianceMap();
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetIBLConvolvedSpecularMap();
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetIBLBRDFLookUpTexture();
	int GetNumIBLMipLevels();


	void Draw(std::shared_ptr<Camera> camera);

private:

	void InitRenderStates();
	void IBLCreateIrradianceMap(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblIrradMapPS);
	void IBLCreateConvolvedSpecularMap(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblSpecConvPS);
	void IBLCreateBRDFLookUpTexture(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblBRDFLookUpPS);


	// Helper for creating a cubemap from 6 individual textures
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateCubemap(
		const wchar_t* right,
		const wchar_t* left,
		const wchar_t* up,
		const wchar_t* down,
		const wchar_t* front,
		const wchar_t* back);

	// Skybox related resources
	std::shared_ptr<SimpleVertexShader> skyVS;
	std::shared_ptr<SimplePixelShader> skyPS;
	
	std::shared_ptr<Mesh> skyMesh;

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> skyRasterState;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> skyDepthState;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> skySRV;

	Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerOptions;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<ID3D11Device> device;

	//IBL
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iblIrradianceCubeMap_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iblConvolvedSpecularCubeMap_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iblBRDFLookUpTexture_;
	int mipLevels_;
	const int specIBLMipLevelsToSkip = 3; // Number of lower mips (1x1, 2x2, etc.) to exclude from the maps
	const int IBLCubeMapFaceSize_ = 256;
	const int IBLLookUpTextureSize_ = 256;
};

