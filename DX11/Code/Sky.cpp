#include "Sky.h"
#include "WICTextureLoader.h"
#include "DDSTextureLoader.h"

using namespace DirectX;

Sky::Sky(
	const wchar_t* cubemapDDSFile, 
	std::shared_ptr<Mesh> mesh,
	std::shared_ptr<SimpleVertexShader> skyVS, 
	std::shared_ptr<SimplePixelShader> skyPS, 
	Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerOptions, 
	Microsoft::WRL::ComPtr<ID3D11Device> device, 
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context)
{
	// Save params
	this->skyMesh = mesh;
	this->device = device;
	this->context = context;
	this->samplerOptions = samplerOptions;
	this->skyVS = skyVS;
	this->skyPS = skyPS;
	this->mipLevels_ = 0; //calculated later

	// Init render states
	InitRenderStates();

	// Load texture
	CreateDDSTextureFromFile(device.Get(), cubemapDDSFile, 0, skySRV.GetAddressOf());
}

Sky::Sky(
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
	std::shared_ptr<SimplePixelShader> _iblBRDFLookUpPS)
{
	// Save params
	this->skyMesh = mesh;
	this->device = device;
	this->context = context;
	this->samplerOptions = samplerOptions;
	this->skyVS = skyVS;
	this->skyPS = skyPS;

	// Init render states
	InitRenderStates();

	// Create texture from 6 images
	skySRV = CreateCubemap(right, left, up, down, front, back);

	mipLevels_ = 0; // calculated later
	IBLCreateIrradianceMap(_fullscreenVS, _iblIrradMapPS);
	//IBLCreateConvolvedSpecularMap( _fullscreenVS, _iblSpecConvPS);
	IBLCreateBRDFLookUpTexture(_fullscreenVS, _iblBRDFLookUpPS);
}

Sky::~Sky()
{
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Sky::GetIBLIrradianceMap()
{
	return iblIrradianceCubeMap_;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Sky::GetIBLConvolvedSpecularMap()
{
	return iblConvolvedSpecularCubeMap_;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Sky::GetIBLBRDFLookUpTexture()
{
	return iblBRDFLookUpTexture_;
}

int Sky::GetNumIBLMipLevels()
{
	return mipLevels_;
}

void Sky::Draw(std::shared_ptr<Camera> camera)
{
	// Change to the sky-specific rasterizer state
	context->RSSetState(skyRasterState.Get());
	context->OMSetDepthStencilState(skyDepthState.Get(), 0);

	// Set the sky shaders
	skyVS->SetShader();
	skyPS->SetShader();

	// Give them proper data
	skyVS->SetMatrix4x4("view", camera->GetView());
	skyVS->SetMatrix4x4("projection", camera->GetProjection());
	skyVS->CopyAllBufferData();

	// Send the proper resources to the pixel shader
	skyPS->SetShaderResourceView("skyTexture", skySRV);
	skyPS->SetSamplerState("samplerOptions", samplerOptions);

	// Set mesh buffers and draw
	skyMesh->SetBuffersAndDraw(context);

	// Reset my rasterizer state to the default
	context->RSSetState(0); // Null (or 0) puts back the defaults
	context->OMSetDepthStencilState(0, 0);
}

void Sky::InitRenderStates()
{
	// Rasterizer to reverse the cull mode
	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.CullMode = D3D11_CULL_FRONT; // Draw the inside instead of the outside!
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.DepthClipEnable = true;
	device->CreateRasterizerState(&rastDesc, skyRasterState.GetAddressOf());

	// Depth state so that we ACCEPT pixels with a depth == 1
	D3D11_DEPTH_STENCIL_DESC depthDesc = {};
	depthDesc.DepthEnable = true;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	device->CreateDepthStencilState(&depthDesc, skyDepthState.GetAddressOf());
}

void Sky::IBLCreateIrradianceMap(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblIrradMapPS)
{
	printf("Making Irradiance Map...");

	Microsoft::WRL::ComPtr<ID3D11Texture2D> finalIrradMap;

	// Create the final irradiance cube texture
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = IBLCubeMapFaceSize_; 			// One of your constants
	texDesc.Height = IBLCubeMapFaceSize_; 			// Same as width
	texDesc.ArraySize = 6; 					// Cube map means 6 textures
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; // Will be used as both
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 		// Basic texture format
	texDesc.MipLevels = 1; 					// No mip chain needed
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE; 	// It's a cube map
	texDesc.SampleDesc.Count = 1; 				// Can't be zero
	device->CreateTexture2D(&texDesc, 0, finalIrradMap.GetAddressOf());

	// Create an SRV for the irradiance texture
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; // Sample as a cube map
	srvDesc.TextureCube.MipLevels = 1; 		// Only 1 mip level
	srvDesc.TextureCube.MostDetailedMip = 0;			// Accessing the first (and only) mip
	srvDesc.Format = texDesc.Format; 	// Same format as texture
	device->CreateShaderResourceView(
		finalIrradMap.Get(), 		// Texture from previous step
		&srvDesc, 				// Description from this step
		iblIrradianceCubeMap_.GetAddressOf());	// Member variable of the Sky class

	// Save current render target and depth buffer
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prevRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> prevDSV;
	context->OMGetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.GetAddressOf());

	// Save current viewport
	unsigned int vpCount = 1;
	D3D11_VIEWPORT prevVP = {};
	context->RSGetViewports(&vpCount, &prevVP);

	// Make sure the viewport matches the texture size
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)IBLCubeMapFaceSize_;
	vp.Height = (float)IBLCubeMapFaceSize_;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vp);

	// Set states that may or may not be set yet
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	_fullscreenVS->SetShader();
	_iblIrradMapPS->SetShader();
	_iblIrradMapPS->SetShaderResourceView("EnvironmentMap", skySRV.Get()); // Skybox texture itself
	_iblIrradMapPS->SetSamplerState("BasicSampler", samplerOptions.Get());


	for (int face = 0; face < 6; face++)
	{
		// Make a render target view for this face
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; // This points to a Texture2D Array
		rtvDesc.Texture2DArray.ArraySize = 1;	// How much of the array do we need access to?
		rtvDesc.Texture2DArray.FirstArraySlice = face;	// Which texture are we rendering into?
		rtvDesc.Texture2DArray.MipSlice = 0;	// Which mip are we rendering into?
		rtvDesc.Format = texDesc.Format; // Same format as texture

		// Create the RTV itself
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
		device->CreateRenderTargetView(finalIrradMap.Get(), &rtvDesc, rtv.GetAddressOf());

		// Clear and set this render target
		float black[4] = {}; // Initialize to all zeroes
		context->ClearRenderTargetView(rtv.Get(), black);
		context->OMSetRenderTargets(1, rtv.GetAddressOf(), 0);

		// Per-face shader data and copy
		_iblIrradMapPS->SetInt("faceIndex", face);
		_iblIrradMapPS->CopyAllBufferData();

		// Render exactly 3 vertices
		context->Draw(3, 0);

		// Ensure we flush the graphics pipe to so that we don't cause 
		// a hardware timeout which can result in a driver crash
		// NOTE: This might make C++ sit and wait for a sec!  Better than a crash!
		context->Flush();

	}
	
	// Restore the old render target and viewport
	context->OMSetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.Get());
	context->RSSetViewports(1, &prevVP);

	printf("Done!\n");
}

void Sky::IBLCreateConvolvedSpecularMap(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblSpecConvPS)
{
	printf("Creating Convolved Specular Map...");

	// Calculate how many mip levels we'll need, potentially skipping a few of the smaller
	// mip levels (1x1, 2x2, etc.) because, with such low resolutions, they're mostly the same.
	// (The +1 is necessary to account for the 1x1 mip level)
	mipLevels_ = max((int)(log2(IBLCubeMapFaceSize_)) + 1 - specIBLMipLevelsToSkip, 1);

	Microsoft::WRL::ComPtr<ID3D11Texture2D> finalSpecConvMap;

	// Create the final conv cube texture
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = IBLCubeMapFaceSize_;
	texDesc.Height = IBLCubeMapFaceSize_;
	texDesc.ArraySize = 6; // Cube map means 6 textures
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // Basic texture format
	texDesc.MipLevels = mipLevels_; // Depends on face size
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE; // It's a cube map
	texDesc.SampleDesc.Count = 1; // Can't be zero
	device->CreateTexture2D(&texDesc, 0, finalSpecConvMap.GetAddressOf());

	// Create an SRV for the texture
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;	// Treat this resource as a cube map
	srvDesc.TextureCube.MipLevels = mipLevels_;			// Depends on face size
	srvDesc.TextureCube.MostDetailedMip = 0;	// Accessing the first (and only) mip
	srvDesc.Format = texDesc.Format;			// Same format as texture
	device->CreateShaderResourceView(finalSpecConvMap.Get(), &srvDesc, GetIBLConvolvedSpecularMap().GetAddressOf());


	// Save current render target and depth buffer
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prevRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> prevDSV;
	context->OMGetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.GetAddressOf());

	// Save current viewport
	unsigned int vpCount = 1;
	D3D11_VIEWPORT prevVP = {};
	context->RSGetViewports(&vpCount, &prevVP);

	// Set states that may or may not be set yet
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set shaders
	_fullscreenVS->SetShader();
	_iblSpecConvPS->SetShader();
	_iblSpecConvPS->SetShaderResourceView("EnvironmentMap", skySRV.Get()); // Skybox texture itself
	_iblSpecConvPS->SetSamplerState("BasicSampler", samplerOptions.Get());

	for (int mip = 0; mip < mipLevels_; mip++)
	{

		// Loop through the six cubemap faces and calculate 
		// the convultion for each, resulting in a full 360 degree
		// irradiance map, once completed
		for (int face = 0; face < 6; face++)
		{
			// Make a render target view for this face
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;	// This points to a Texture2D Array
			rtvDesc.Texture2DArray.ArraySize = 1;			// How much of the array do we have access to?
			rtvDesc.Texture2DArray.FirstArraySlice = face;	// Which texture are we rendering into?
			rtvDesc.Texture2DArray.MipSlice = mip;			// Which mip of that texture are we rendering into?
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// Same format as accum texture

			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
			device->CreateRenderTargetView(finalSpecConvMap.Get(), &rtvDesc, rtv.GetAddressOf());

			// Clear and set this render target
			float black[4] = {}; // Initialize to all zeroes
			context->ClearRenderTargetView(rtv.Get(), black);
			context->OMSetRenderTargets(1, rtv.GetAddressOf(), 0);

			// Create a viewport that matches the size of this MIP
			D3D11_VIEWPORT vp = {};
			vp.Width = (float)pow(2, mipLevels_ + specIBLMipLevelsToSkip - 1 - mip);
			vp.Height = vp.Width; // Always square
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			context->RSSetViewports(1, &vp);

			// Handle per-face shader data and copy
			_iblSpecConvPS->SetFloat("roughness", mip / (float)(mipLevels_ - 1));
			_iblSpecConvPS->SetInt("faceIndex", face);
			_iblSpecConvPS->SetInt("mipLevel", mip);
			_iblSpecConvPS->CopyAllBufferData();

			// Render exactly 3 vertices
			context->Draw(3, 0);

			// Ensure we flush the graphics pipe to 
			// so that we don't cause a hardware timeout
			// which can result in a driver crash
			// NOTE: This might make C++ sit and wait!  Better than a crash!
			context->Flush();
		}

	}

	// Restore the old render target and viewport
	context->OMSetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.Get());
	context->RSSetViewports(1, &prevVP);

	printf("Done!\n");
}

void Sky::IBLCreateBRDFLookUpTexture(std::shared_ptr<SimpleVertexShader> _fullscreenVS, std::shared_ptr<SimplePixelShader> _iblBRDFLookUpPS)
{
	Microsoft::WRL::ComPtr<ID3D11Texture2D> finalBRDFlookupTexture;

	// Create the final look up texture
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = IBLLookUpTextureSize_;
	texDesc.Height = IBLLookUpTextureSize_;
	texDesc.ArraySize = 1; // Single texture
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.Format = DXGI_FORMAT_R16G16_UNORM;  // Only two channels, each of which is double the precision
	texDesc.MipLevels = 1; // Just one mip level
	texDesc.MiscFlags = 0; // NOT a cube map!
	texDesc.SampleDesc.Count = 1; // Can't be zero
	device->CreateTexture2D(&texDesc, 0, finalBRDFlookupTexture.GetAddressOf());

	// Create an SRV for the BRDF look-up texture
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;	// Just a regular 2d texture
	srvDesc.Texture2D.MipLevels = 1;			// Just one
	srvDesc.Texture2D.MostDetailedMip = 0;		// Accessing the first (and only) mip
	srvDesc.Format = texDesc.Format;			// Same format as texture
	device->CreateShaderResourceView(finalBRDFlookupTexture.Get(), &srvDesc, iblBRDFLookUpTexture_.GetAddressOf());

	// Save current render target and depth buffer
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prevRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> prevDSV;
	context->OMGetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.GetAddressOf());

	// Save current viewport
	unsigned int vpCount = 1;
	D3D11_VIEWPORT prevVP = {};
	context->RSGetViewports(&vpCount, &prevVP);

	// Create a viewport that matches the size of this texture
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)IBLLookUpTextureSize_;
	vp.Height = (float)IBLLookUpTextureSize_;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vp);

	// Set states that may or may not be set yet
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set Shaders
	_fullscreenVS->SetShader();
	_iblBRDFLookUpPS->SetShader();

	// Make a render target view for this whole texture
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;	// This points to a Texture2D
	rtvDesc.Texture2D.MipSlice = 0;							// Which mip of that texture are we rendering into?
	rtvDesc.Format = texDesc.Format;						// Match the format of the texture

	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
	device->CreateRenderTargetView(finalBRDFlookupTexture.Get(), &rtvDesc, rtv.GetAddressOf());

	// Clear and set this render target
	float black[4] = {}; // Initialize to all zeroes
	context->ClearRenderTargetView(rtv.Get(), black);
	context->OMSetRenderTargets(1, rtv.GetAddressOf(), 0);

	// Render exactly 3 vertices
	context->Draw(3, 0);

	// Ensure we flush the graphics pipe to 
	// so that we don't cause a hardware timeout
	// which can result in a driver crash
	// NOTE: This might make C++ sit and wait!  Better than a crash!
	context->Flush();

	// Restore render target & viewport
	context->OMSetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.Get());
	context->RSSetViewports(1, &prevVP);

	printf("Done!\n");
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Sky::CreateCubemap(const wchar_t* right, const wchar_t* left, const wchar_t* up, const wchar_t* down, const wchar_t* front, const wchar_t* back)
{
	// Load the 6 textures into an array.
	// - We need references to the TEXTURES, not the SHADER RESOURCE VIEWS!
	// - Specifically NOT generating mipmaps, as we don't need them for the sky!
	// - Order matters here!  +X, -X, +Y, -Y, +Z, -Z
	Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[6] = {};
	CreateWICTextureFromFile(device.Get(), right, (ID3D11Resource**)textures[0].GetAddressOf(), 0);
	CreateWICTextureFromFile(device.Get(), left, (ID3D11Resource**)textures[1].GetAddressOf(), 0);
	CreateWICTextureFromFile(device.Get(), up, (ID3D11Resource**)textures[2].GetAddressOf(), 0);
	CreateWICTextureFromFile(device.Get(), down, (ID3D11Resource**)textures[3].GetAddressOf(), 0);
	CreateWICTextureFromFile(device.Get(), front, (ID3D11Resource**)textures[4].GetAddressOf(), 0);
	CreateWICTextureFromFile(device.Get(), back, (ID3D11Resource**)textures[5].GetAddressOf(), 0);

	// We'll assume all of the textures are the same color format and resolution,
	// so get the description of the first shader resource view
	D3D11_TEXTURE2D_DESC faceDesc = {};
	textures[0]->GetDesc(&faceDesc);

	// Describe the resource for the cube map, which is simply 
	// a "texture 2d array".  This is a special GPU resource format, 
	// NOT just a C++ array of textures!!!
	D3D11_TEXTURE2D_DESC cubeDesc = {};
	cubeDesc.ArraySize = 6; // Cube map!
	cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // We'll be using as a texture in a shader
	cubeDesc.CPUAccessFlags = 0; // No read back
	cubeDesc.Format = faceDesc.Format; // Match the loaded texture's color format
	cubeDesc.Width = faceDesc.Width;  // Match the size
	cubeDesc.Height = faceDesc.Height; // Match the size
	cubeDesc.MipLevels = 1; // Only need 1
	cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE; // This should be treated as a CUBE, not 6 separate textures
	cubeDesc.Usage = D3D11_USAGE_DEFAULT; // Standard usage
	cubeDesc.SampleDesc.Count = 1;
	cubeDesc.SampleDesc.Quality = 0;

	// Create the actual texture resource
	Microsoft::WRL::ComPtr<ID3D11Texture2D> cubeMapTexture;
	device->CreateTexture2D(&cubeDesc, 0, &cubeMapTexture);

	// Loop through the individual face textures and copy them,
	// one at a time, to the cube map texure
	for (int i = 0; i < 6; i++)
	{
		// Calculate the subresource position to copy into
		unsigned int subresource = D3D11CalcSubresource(
			0,	// Which mip (zero, since there's only one)
			i,	// Which array element?
			1); // How many mip levels are in the texture?

		// Copy from one resource (texture) to another
		context->CopySubresourceRegion(
			cubeMapTexture.Get(), // Destination resource
			subresource,		// Dest subresource index (one of the array elements)
			0, 0, 0,			// XYZ location of copy
			textures[i].Get(),	// Source resource
			0,					// Source subresource index (we're assuming there's only one)
			0);					// Source subresource "box" of data to copy (zero means the whole thing)
	}

	// At this point, all of the faces have been copied into the 
	// cube map texture, so we can describe a shader resource view for it
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = cubeDesc.Format; // Same format as texture
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; // Treat this as a cube!
	srvDesc.TextureCube.MipLevels = 1;	// Only need access to 1 mip
	srvDesc.TextureCube.MostDetailedMip = 0; // Index of the first mip we want to see

	// Make the SRV
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubeSRV;
	device->CreateShaderResourceView(cubeMapTexture.Get(), &srvDesc, cubeSRV.GetAddressOf());

	// Send back the SRV, which is what we need for our shaders
	return cubeSRV;
}
