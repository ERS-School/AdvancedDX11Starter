#pragma once

#include "DXCore.h"
#include "Camera.h"
#include "GameEntity.h"

#include <DirectXMath.h>
#include <wrl/client.h> // Used for ComPtr - a smart pointer for COM objects
#include <memory>
#include <vector>

#include "Lights.h"


class Game 
	: public DXCore
{

public:
	Game(HINSTANCE hInstance);
	~Game();

	// Overridden setup and game loop methods, which
	// will be called automatically
	void Init();
	void OnResize();
	void Update(float deltaTime, float totalTime);
	void Draw(float deltaTime, float totalTime);

private:

	// Should we use vsync to limit the frame rate?
	bool vsync;

	// Initialization helper methods - feel free to customize, combine, etc.
	void CreateBasicGeometry();
	void CreateRootSigAndPipelineState();
	void CreateLights();

	// Note the usage of ComPtr below
	//  - This is a smart pointer for objects that abide by the
	//    Component Object Model, which DirectX objects do
	//  - More info here: https://github.com/Microsoft/DirectXTK/wiki/ComPtr

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vbView;
	D3D12_INDEX_BUFFER_VIEW ibView;


	int lightCount;
	std::vector<Light> lights;
	std::shared_ptr<Camera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
};
