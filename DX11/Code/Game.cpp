
#include <stdlib.h>     // For seeding random and rand()
#include <time.h>       // For grabbing time (to seed random)

#include "Game.h"
#include "Vertex.h"
#include "Input.h"
#include "Renderer.h"

#include "Imgui\imgui.h"
#include "Imgui\imgui_impl_dx11.h"
#include "Imgui\imgui_impl_win32.h"

#include "WICTextureLoader.h"


// Needed for a helper function to read compiled shader files from the hard drive
#pragma comment(lib, "d3dcompiler.lib")
#include <d3dcompiler.h>

// For the DirectX Math library
using namespace DirectX;

// Helper macro for getting a float between min and max
#define RandomRange(min, max) (float)rand() / RAND_MAX * (max - min) + min

// Helper macros for making texture and shader loading code more succinct
#define LoadTexture(file, srv) CreateWICTextureFromFile(device.Get(), context.Get(), GetFullPathTo_Wide(file).c_str(), 0, srv.GetAddressOf())
#define LoadShader(type, file) std::make_shared<type>(device.Get(), context.Get(), GetFullPathTo_Wide(file).c_str())


// --------------------------------------------------------
// Constructor
//
// DXCore (base class) constructor will set up underlying fields.
// DirectX itself, and our window, are not ready yet!
//
// hInstance - the application's OS-level handle (unique ID)
// --------------------------------------------------------
Game::Game(HINSTANCE hInstance)
	: DXCore(
		hInstance,		   // The application's handle
		"DirectX Game",	   // Text for the window's title bar
		1280,			   // Width of the window's client area
		720,			   // Height of the window's client area
		true),			   // Show extra stats (fps) in title bar?
	camera(0),
	sky(0),
	spriteBatch(0),
	lightCount(0),
	arial(0)
{
	// Seed random
	srand((unsigned int)time(0));

#if defined(DEBUG) || defined(_DEBUG)
	// Do we want a console window?  Probably only in debug mode
	CreateConsoleWindow(500, 120, 32, 120);
	printf("Console window created successfully.  Feel free to printf() here.\n");
#endif

}

// --------------------------------------------------------
// Destructor - Clean up anything our game has created:
//  - Release all DirectX objects created here
//  - Delete any objects to prevent memory leaks
// --------------------------------------------------------
Game::~Game()
{
	// Note: Since we're using smart pointers (ComPtr),
	// we don't need to explicitly clean up those DirectX objects
	// - If we weren't using smart pointers, we'd need
	//   to call Release() on each DirectX object

	// ImGui Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

// --------------------------------------------------------
// Called once per program, after DirectX and the window
// are initialized but before the game loop.
// --------------------------------------------------------
void Game::Init()
{
	// Asset loading and entity creation (including skybox)
	LoadAssetsAndCreateEntities();
	
	// Tell the input assembler stage of the pipeline what kind of
	// geometric primitives (points, lines or triangles) we want to draw.  
	// Essentially: "What kind of shape should the GPU draw with our data?"
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up lights initially
	lightCount = 64;
	GenerateLights();

	// Make our camera
	camera = std::make_shared<Camera>(
		0.0f, 0.0f, -10.0f,	// Position
		3.0f,		// Move speed
		1.0f,		// Mouse look
		this->width / (float)this->height); // Aspect ratio

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark(); // pick a style
	// Set up platform/render backends
	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX11_Init(device.Get(), context.Get());


	// Create the render last once all it needs has been created
	renderer = std::make_shared<Renderer>(device, context, 
										swapChain,
										backBufferRTV, depthStencilView,
										width, height,
										sky,
										entities, lights,
										lightMesh,
										lightVS,
										lightPS,
										arial,
										spriteBatch);
}


// --------------------------------------------------------
// Load all assets and create materials, entities, etc.
// --------------------------------------------------------
void Game::LoadAssetsAndCreateEntities()
{
	// Load shaders using our succinct LoadShader() macro
	std::shared_ptr<SimpleVertexShader> vertexShader	= LoadShader(SimpleVertexShader, L"VertexShader.cso");
	std::shared_ptr<SimplePixelShader> pixelShader		= LoadShader(SimplePixelShader, L"PixelShader.cso");
	std::shared_ptr<SimplePixelShader> pixelShaderPBR	= LoadShader(SimplePixelShader, L"PixelShaderPBR.cso");
	std::shared_ptr<SimplePixelShader> solidColorPS		= LoadShader(SimplePixelShader, L"SolidColorPS.cso");
	std::shared_ptr<SimpleVertexShader> fullscreenVS		= LoadShader(SimpleVertexShader, L"FullscreenVS.cso");
	std::shared_ptr<SimplePixelShader> iblIrradMapPS		= LoadShader(SimplePixelShader, L"IBLIrradianceMapPS.cso");
	std::shared_ptr<SimplePixelShader> iblSpecConvPS		= LoadShader(SimplePixelShader, L"IBLSpecularConvolutionPS.cso");
	std::shared_ptr<SimplePixelShader> iblBRDFlookupPS		= LoadShader(SimplePixelShader, L"IBLBRDFLookUpTablePS.cso");
	
	std::shared_ptr<SimpleVertexShader> skyVS = LoadShader(SimpleVertexShader, L"SkyVS.cso");
	std::shared_ptr<SimplePixelShader> skyPS  = LoadShader(SimplePixelShader, L"SkyPS.cso");

	// Set up the sprite batch and load the sprite font
	spriteBatch = std::make_shared<SpriteBatch>(context.Get());
	arial = std::make_shared<SpriteFont>(device.Get(), GetFullPathTo_Wide(L"../../Assets/Textures/arial.spritefont").c_str());

	// Make the meshes
	std::shared_ptr<Mesh> sphereMesh = std::make_shared<Mesh>(GetFullPathTo("../../Assets/Models/sphere.obj").c_str(), device);
	std::shared_ptr<Mesh> helixMesh = std::make_shared<Mesh>(GetFullPathTo("../../Assets/Models/helix.obj").c_str(), device);
	std::shared_ptr<Mesh> cubeMesh = std::make_shared<Mesh>(GetFullPathTo("../../Assets/Models/cube.obj").c_str(), device);
	std::shared_ptr<Mesh> coneMesh = std::make_shared<Mesh>(GetFullPathTo("../../Assets/Models/cone.obj").c_str(), device);
	
	// Declare the textures we'll need
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cobbleA,  cobbleN,  cobbleR,  cobbleM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> floorA,  floorN,  floorR,  floorM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> paintA,  paintN,  paintR,  paintM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scratchedA,  scratchedN,  scratchedR,  scratchedM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bronzeA,  bronzeN,  bronzeR,  bronzeM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> roughA,  roughN,  roughR,  roughM;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> woodA,  woodN,  woodR,  woodM;


	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white, gray75, gray50, gray25, black, flatNormals;
	LoadTexture(L"../../Assets/Textures/SolidColors/white.png", white);
	LoadTexture(L"../../Assets/Textures/SolidColors/graySeventyFivePercent.png", gray75);
	LoadTexture(L"../../Assets/Textures/SolidColors/grayFiftyPercent.png", gray50);
	LoadTexture(L"../../Assets/Textures/SolidColors/grayTwentyFivePercent.png", gray25);
	LoadTexture(L"../../Assets/Textures/SolidColors/black.png", black);
	LoadTexture(L"../../Assets/Textures/SolidColors/flatNormals.png", flatNormals);


	// Load the textures using our succinct LoadTexture() macro
	LoadTexture(L"../../Assets/Textures/cobblestone_albedo.png", cobbleA);
	LoadTexture(L"../../Assets/Textures/cobblestone_normals.png", cobbleN);
	LoadTexture(L"../../Assets/Textures/cobblestone_roughness.png", cobbleR);
	LoadTexture(L"../../Assets/Textures/cobblestone_metal.png", cobbleM);

	LoadTexture(L"../../Assets/Textures/floor_albedo.png", floorA);
	LoadTexture(L"../../Assets/Textures/floor_normals.png", floorN);
	LoadTexture(L"../../Assets/Textures/floor_roughness.png", floorR);
	LoadTexture(L"../../Assets/Textures/floor_metal.png", floorM);
	
	LoadTexture(L"../../Assets/Textures/paint_albedo.png", paintA);
	LoadTexture(L"../../Assets/Textures/paint_normals.png", paintN);
	LoadTexture(L"../../Assets/Textures/paint_roughness.png", paintR);
	LoadTexture(L"../../Assets/Textures/paint_metal.png", paintM);
	
	LoadTexture(L"../../Assets/Textures/scratched_albedo.png", scratchedA);
	LoadTexture(L"../../Assets/Textures/scratched_normals.png", scratchedN);
	LoadTexture(L"../../Assets/Textures/scratched_roughness.png", scratchedR);
	LoadTexture(L"../../Assets/Textures/scratched_metal.png", scratchedM);
	
	LoadTexture(L"../../Assets/Textures/bronze_albedo.png", bronzeA);
	LoadTexture(L"../../Assets/Textures/bronze_normals.png", bronzeN);
	LoadTexture(L"../../Assets/Textures/bronze_roughness.png", bronzeR);
	LoadTexture(L"../../Assets/Textures/bronze_metal.png", bronzeM);
	
	LoadTexture(L"../../Assets/Textures/rough_albedo.png", roughA);
	LoadTexture(L"../../Assets/Textures/rough_normals.png", roughN);
	LoadTexture(L"../../Assets/Textures/rough_roughness.png", roughR);
	LoadTexture(L"../../Assets/Textures/rough_metal.png", roughM);
	
	LoadTexture(L"../../Assets/Textures/wood_albedo.png", woodA);
	LoadTexture(L"../../Assets/Textures/wood_normals.png", woodN);
	LoadTexture(L"../../Assets/Textures/wood_roughness.png", woodR);
	LoadTexture(L"../../Assets/Textures/wood_metal.png", woodM);

	// Describe and create our sampler state
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampDesc.MaxAnisotropy = 16;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	device->CreateSamplerState(&sampDesc, samplerOptions.GetAddressOf());
	// Describe and create our sampler state
	D3D11_SAMPLER_DESC sampDesc2 = {};
	sampDesc2.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc2.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc2.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc2.Filter = D3D11_FILTER_ANISOTROPIC;
	sampDesc2.MaxAnisotropy = 16;
	sampDesc2.MaxLOD = D3D11_FLOAT32_MAX;
	device->CreateSamplerState(&sampDesc2, clampSamplerOptions.GetAddressOf());


	// Create the sky using 6 images
	sky = std::make_shared<Sky>(
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\right.png").c_str(),
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\left.png").c_str(),
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\up.png").c_str(),
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\down.png").c_str(),
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\front.png").c_str(),
		GetFullPathTo_Wide(L"..\\..\\Assets\\Skies\\Clouds Blue\\back.png").c_str(),
		cubeMesh,
		skyVS,
		skyPS,
		samplerOptions,
		device,
		context,
		fullscreenVS,
		iblIrradMapPS,
		iblSpecConvPS,
		iblBRDFlookupPS);

	// Create non-PBR materials
	std::shared_ptr<Material> cobbleMat2x = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	cobbleMat2x->AddSampler("BasicSampler", samplerOptions);
	cobbleMat2x->AddTextureSRV("Albedo", cobbleA);
	cobbleMat2x->AddTextureSRV("NormalMap", cobbleN);
	cobbleMat2x->AddTextureSRV("RoughnessMap", cobbleR);

	std::shared_ptr<Material> cobbleMat4x = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(4, 4));
	cobbleMat4x->AddSampler("BasicSampler", samplerOptions);
	cobbleMat4x->AddTextureSRV("Albedo", cobbleA);
	cobbleMat4x->AddTextureSRV("NormalMap", cobbleN);
	cobbleMat4x->AddTextureSRV("RoughnessMap", cobbleR);

	std::shared_ptr<Material> floorMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	floorMat->AddSampler("BasicSampler", samplerOptions);
	floorMat->AddTextureSRV("Albedo", floorA);
	floorMat->AddTextureSRV("NormalMap", floorN);
	floorMat->AddTextureSRV("RoughnessMap", floorR);

	std::shared_ptr<Material> paintMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	paintMat->AddSampler("BasicSampler", samplerOptions);
	paintMat->AddTextureSRV("Albedo", paintA);
	paintMat->AddTextureSRV("NormalMap", paintN);
	paintMat->AddTextureSRV("RoughnessMap", paintR);

	std::shared_ptr<Material> scratchedMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	scratchedMat->AddSampler("BasicSampler", samplerOptions);
	scratchedMat->AddTextureSRV("Albedo", scratchedA);
	scratchedMat->AddTextureSRV("NormalMap", scratchedN);
	scratchedMat->AddTextureSRV("RoughnessMap", scratchedR);

	std::shared_ptr<Material> bronzeMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	bronzeMat->AddSampler("BasicSampler", samplerOptions);
	bronzeMat->AddTextureSRV("Albedo", bronzeA);
	bronzeMat->AddTextureSRV("NormalMap", bronzeN);
	bronzeMat->AddTextureSRV("RoughnessMap", bronzeR);

	std::shared_ptr<Material> roughMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	roughMat->AddSampler("BasicSampler", samplerOptions);
	roughMat->AddTextureSRV("Albedo", roughA);
	roughMat->AddTextureSRV("NormalMap", roughN);
	roughMat->AddTextureSRV("RoughnessMap", roughR);

	std::shared_ptr<Material> woodMat = std::make_shared<Material>(pixelShader, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	woodMat->AddSampler("BasicSampler", samplerOptions);
	woodMat->AddTextureSRV("Albedo", woodA);
	woodMat->AddTextureSRV("NormalMap", woodN);
	woodMat->AddTextureSRV("RoughnessMap", woodR);


	// Create PBR materials
	std::shared_ptr<Material> cobbleMat2xPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	cobbleMat2xPBR->AddSampler("BasicSampler", samplerOptions);
	cobbleMat2xPBR->AddSampler("ClampSampler", clampSamplerOptions);
	cobbleMat2xPBR->AddTextureSRV("Albedo", cobbleA);
	cobbleMat2xPBR->AddTextureSRV("NormalMap", cobbleN);
	cobbleMat2xPBR->AddTextureSRV("RoughnessMap", cobbleR);
	cobbleMat2xPBR->AddTextureSRV("MetalMap", cobbleM);
	cobbleMat2xPBR->AddTextureSRV("MetalMap", cobbleM);
	cobbleMat2xPBR->AddTextureSRV("MetalMap", cobbleM);
	cobbleMat2xPBR->AddTextureSRV("MetalMap", cobbleM);
	cobbleMat2xPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	cobbleMat2xPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	cobbleMat2xPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> cobbleMat4xPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(4, 4));
	cobbleMat4xPBR->AddSampler("BasicSampler", samplerOptions);
	cobbleMat4xPBR->AddSampler("ClampSampler", clampSamplerOptions);
	cobbleMat4xPBR->AddTextureSRV("Albedo", cobbleA);
	cobbleMat4xPBR->AddTextureSRV("NormalMap", cobbleN);
	cobbleMat4xPBR->AddTextureSRV("RoughnessMap", cobbleR);
	cobbleMat4xPBR->AddTextureSRV("MetalMap", cobbleM);
	cobbleMat4xPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	cobbleMat4xPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	cobbleMat4xPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> floorMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	floorMatPBR->AddSampler("BasicSampler", samplerOptions);
	floorMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	floorMatPBR->AddTextureSRV("Albedo", floorA);
	floorMatPBR->AddTextureSRV("NormalMap", floorN);
	floorMatPBR->AddTextureSRV("RoughnessMap", floorR);
	floorMatPBR->AddTextureSRV("MetalMap", floorM);
	floorMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	floorMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	floorMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> paintMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	paintMatPBR->AddSampler("BasicSampler", samplerOptions);
	paintMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	paintMatPBR->AddTextureSRV("Albedo", paintA);
	paintMatPBR->AddTextureSRV("NormalMap", paintN);
	paintMatPBR->AddTextureSRV("RoughnessMap", paintR);
	paintMatPBR->AddTextureSRV("MetalMap", paintM);
	paintMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	paintMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	paintMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> scratchedMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	scratchedMatPBR->AddSampler("BasicSampler", samplerOptions);
	scratchedMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	scratchedMatPBR->AddTextureSRV("Albedo", scratchedA);
	scratchedMatPBR->AddTextureSRV("NormalMap", scratchedN);
	scratchedMatPBR->AddTextureSRV("RoughnessMap", scratchedR);
	scratchedMatPBR->AddTextureSRV("MetalMap", scratchedM);
	scratchedMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	scratchedMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	scratchedMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> bronzeMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	bronzeMatPBR->AddSampler("BasicSampler", samplerOptions);
	bronzeMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	bronzeMatPBR->AddTextureSRV("Albedo", bronzeA);
	bronzeMatPBR->AddTextureSRV("NormalMap", bronzeN);
	bronzeMatPBR->AddTextureSRV("RoughnessMap", bronzeR);
	bronzeMatPBR->AddTextureSRV("MetalMap", bronzeM);
	bronzeMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	bronzeMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	bronzeMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> roughMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	roughMatPBR->AddSampler("BasicSampler", samplerOptions);
	roughMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	roughMatPBR->AddTextureSRV("Albedo", roughA);
	roughMatPBR->AddTextureSRV("NormalMap", roughN);
	roughMatPBR->AddTextureSRV("RoughnessMap", roughR);
	roughMatPBR->AddTextureSRV("MetalMap", roughM);
	roughMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	roughMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	roughMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());

	std::shared_ptr<Material> woodMatPBR = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	woodMatPBR->AddSampler("BasicSampler", samplerOptions);
	woodMatPBR->AddSampler("ClampSampler", clampSamplerOptions);
	woodMatPBR->AddTextureSRV("Albedo", woodA);
	woodMatPBR->AddTextureSRV("NormalMap", woodN);
	woodMatPBR->AddTextureSRV("RoughnessMap", woodR);
	woodMatPBR->AddTextureSRV("MetalMap", woodM);
	woodMatPBR->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	woodMatPBR->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	woodMatPBR->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());


	std::shared_ptr<Material> iblTestMat_shinyMetal = std::make_shared<Material>(pixelShaderPBR, vertexShader, XMFLOAT3(1, 1, 1), XMFLOAT2(2, 2));
	iblTestMat_shinyMetal->AddSampler("BasicSampler", samplerOptions);
	iblTestMat_shinyMetal->AddSampler("ClampSampler", clampSamplerOptions);
	iblTestMat_shinyMetal->AddTextureSRV("Albedo", white);
	iblTestMat_shinyMetal->AddTextureSRV("NormalMap", flatNormals);
	iblTestMat_shinyMetal->AddTextureSRV("RoughnessMap", black);
	iblTestMat_shinyMetal->AddTextureSRV("MetalMap", black);
	iblTestMat_shinyMetal->AddTextureSRV("BrdfLookUpMap", sky->GetIBLBRDFLookUpTexture());
	iblTestMat_shinyMetal->AddTextureSRV("IrradianceIBLMap", sky->GetIBLIrradianceMap());
	iblTestMat_shinyMetal->AddTextureSRV("SpecularIBLMap", sky->GetIBLConvolvedSpecularMap());


	// === Create the PBR entities =====================================
	std::shared_ptr<GameEntity> cobSpherePBR = std::make_shared<GameEntity>(sphereMesh, cobbleMat2xPBR);
	cobSpherePBR->GetTransform()->SetPosition(-6, 2, 0);

	std::shared_ptr<GameEntity> floorSpherePBR = std::make_shared<GameEntity>(sphereMesh, floorMatPBR);
	floorSpherePBR->GetTransform()->SetPosition(-4, 2, 0);

	std::shared_ptr<GameEntity> paintSpherePBR = std::make_shared<GameEntity>(sphereMesh, paintMatPBR);
	paintSpherePBR->GetTransform()->SetPosition(-2, 2, 0);

	std::shared_ptr<GameEntity> scratchSpherePBR = std::make_shared<GameEntity>(sphereMesh, scratchedMatPBR);
	scratchSpherePBR->GetTransform()->SetPosition(0, 2, 0);

	std::shared_ptr<GameEntity> bronzeSpherePBR = std::make_shared<GameEntity>(sphereMesh, bronzeMatPBR);
	bronzeSpherePBR->GetTransform()->SetPosition(2, 2, 0);

	std::shared_ptr<GameEntity> roughSpherePBR = std::make_shared<GameEntity>(sphereMesh, roughMatPBR);
	roughSpherePBR->GetTransform()->SetPosition(4, 2, 0);

	std::shared_ptr<GameEntity> woodSpherePBR = std::make_shared<GameEntity>(sphereMesh, woodMatPBR);
	woodSpherePBR->GetTransform()->SetPosition(6, 2, 0);

	std::shared_ptr<GameEntity> shinyMetalSpherePBR = std::make_shared<GameEntity>(sphereMesh, iblTestMat_shinyMetal);
	shinyMetalSpherePBR->GetTransform()->SetPosition(6, 4, 0);

	entities.push_back(cobSpherePBR);
	entities.push_back(floorSpherePBR);
	entities.push_back(paintSpherePBR);
	entities.push_back(scratchSpherePBR);
	entities.push_back(bronzeSpherePBR);
	entities.push_back(roughSpherePBR);
	entities.push_back(woodSpherePBR);
	entities.push_back(shinyMetalSpherePBR);

	// Create the non-PBR entities ==============================
	std::shared_ptr<GameEntity> cobSphere = std::make_shared<GameEntity>(sphereMesh, cobbleMat2x);
	cobSphere->GetTransform()->SetPosition(-6, -2, 0);

	std::shared_ptr<GameEntity> floorSphere = std::make_shared<GameEntity>(sphereMesh, floorMat);
	floorSphere->GetTransform()->SetPosition(-4, -2, 0);

	std::shared_ptr<GameEntity> paintSphere = std::make_shared<GameEntity>(sphereMesh, paintMat);
	paintSphere->GetTransform()->SetPosition(-2, -2, 0);

	std::shared_ptr<GameEntity> scratchSphere = std::make_shared<GameEntity>(sphereMesh, scratchedMat);
	scratchSphere->GetTransform()->SetPosition(0, -2, 0);

	std::shared_ptr<GameEntity> bronzeSphere = std::make_shared<GameEntity>(sphereMesh, bronzeMat);
	bronzeSphere->GetTransform()->SetPosition(2, -2, 0);

	std::shared_ptr<GameEntity> roughSphere = std::make_shared<GameEntity>(sphereMesh, roughMat);
	roughSphere->GetTransform()->SetPosition(4, -2, 0);

	std::shared_ptr<GameEntity> woodSphere = std::make_shared<GameEntity>(sphereMesh, woodMat);
	woodSphere->GetTransform()->SetPosition(6, -2, 0);

	entities.push_back(cobSphere);
	entities.push_back(floorSphere);
	entities.push_back(paintSphere);
	entities.push_back(scratchSphere);
	entities.push_back(bronzeSphere);
	entities.push_back(roughSphere);
	entities.push_back(woodSphere);


	// Save assets needed for drawing point lights
	lightMesh = sphereMesh;
	lightVS = vertexShader;
	lightPS = solidColorPS;
}


// --------------------------------------------------------
// Generates the lights in the scene: 3 directional lights
// and many random point lights.
// --------------------------------------------------------
void Game::GenerateLights()
{
	// Reset
	lights.clear();

	// Setup directional lights
	Light dir1 = {};
	dir1.Type = LIGHT_TYPE_DIRECTIONAL;
	dir1.Direction = XMFLOAT3(1, -1, 1);
	dir1.Color = XMFLOAT3(0.8f, 0.8f, 0.8f);
	dir1.Intensity = 1.0f;

	Light dir2 = {};
	dir2.Type = LIGHT_TYPE_DIRECTIONAL;
	dir2.Direction = XMFLOAT3(-1, -0.25f, 0);
	dir2.Color = XMFLOAT3(0.2f, 0.2f, 0.2f);
	dir2.Intensity = 1.0f;

	Light dir3 = {};
	dir3.Type = LIGHT_TYPE_DIRECTIONAL;
	dir3.Direction = XMFLOAT3(0, -1, 1);
	dir3.Color = XMFLOAT3(0.2f, 0.2f, 0.2f);
	dir3.Intensity = 1.0f;

	// Add light to the list
	lights.push_back(dir1);
	lights.push_back(dir2);
	lights.push_back(dir3);

	// Create the rest of the lights
	while (lights.size() < lightCount)
	{
		Light point = {};
		point.Type = LIGHT_TYPE_POINT;
		point.Position = XMFLOAT3(RandomRange(-10.0f, 10.0f), RandomRange(-5.0f, 5.0f), RandomRange(-10.0f, 10.0f));
		point.Color = XMFLOAT3(RandomRange(0, 1), RandomRange(0, 1), RandomRange(0, 1));
		point.Range = RandomRange(5.0f, 10.0f);
		point.Intensity = RandomRange(0.1f, 3.0f);

		// Add to the list
		lights.push_back(point);
	}

}



// --------------------------------------------------------
// Handle resizing DirectX "stuff" to match the new window size.
// For instance, updating our projection matrix's aspect ratio.
// --------------------------------------------------------
void Game::OnResize()
{
	renderer->PreResize();

	// Handle base-level DX resize stuff
	DXCore::OnResize();

	renderer->PostResize(width, height, backBufferRTV, depthStencilView);
	// Update our projection matrix to match the new aspect ratio
	if (camera)
		camera->UpdateProjectionMatrix(this->width / (float)this->height);
}

// --------------------------------------------------------
// Update your game here - user input, move objects, AI, etc.
// --------------------------------------------------------
void Game::Update(float deltaTime, float totalTime)
{
	Input& input = Input::GetInstance();

	// Update the debug gui
	renderer->UpdateImGui(deltaTime);

	// Update the camera
	camera->Update(deltaTime);

	// Check individual input
	if (input.KeyDown(VK_ESCAPE)) Quit();
	if (input.KeyPress(VK_TAB)) GenerateLights();
}

// --------------------------------------------------------
// Clear the screen, redraw everything, present to the user
// --------------------------------------------------------
void Game::Draw(float deltaTime, float totalTime)
{
	renderer->Render(camera);
}
