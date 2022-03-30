#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <memory>
#include <SpriteBatch.h>
#include <SpriteFont.h>

#include "Sky.h"
#include "GameEntity.h"
#include "Lights.h"
#include "SimpleShader.h"

class Renderer
{
private:
	Microsoft::WRL::ComPtr<ID3D11Device> device; // For creating new resources
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context; // For issues rendering commands
	Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain; // For end-of-frame Present() calls
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRTV; // The back buffer
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthBufferDSV; // The depth buffer
	unsigned int windowWidth; // The current width of the window
	unsigned int windowHeight; // The current height of the window
	std::shared_ptr<Sky> sky; // Pointer to the skybox object created in Game
	std::vector <std::shared_ptr<GameEntity>>& entities; // Reference to the Entity list in Game
	std::vector<Light>& lights; // Reference to the Light list in Game

	// Text & ui
	std::shared_ptr<DirectX::SpriteFont> arial;
	std::shared_ptr<DirectX::SpriteBatch> spriteBatch;

	// Camera used this frame
	std::shared_ptr<Camera> camera;
	bool drawDebugPointLights;

	// These will be loaded along with other assets and
	// saved to these variables for ease of access
	std::shared_ptr<Mesh> lightMesh;
	std::shared_ptr<SimpleVertexShader> lightVS;
	std::shared_ptr<SimplePixelShader> lightPS;


public:
	Renderer(Microsoft::WRL::ComPtr<ID3D11Device> _device,
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context,
			Microsoft::WRL::ComPtr<IDXGISwapChain> _swapChain,
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV,
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthBufferDSV,
			unsigned int _windowWidth,
			unsigned int _windowHeight,
			std::shared_ptr<Sky> _sky,
			std::vector<std::shared_ptr<GameEntity>>& _entities,
			std::vector<Light>& _lights,
			std::shared_ptr<Mesh> _lightMesh,
			std::shared_ptr<SimpleVertexShader> _lightVS,
			std::shared_ptr<SimplePixelShader> _lightPS,
			std::shared_ptr<DirectX::SpriteFont> _arial,
			std::shared_ptr<DirectX::SpriteBatch> _spriteBatch
			);


	void PreResize();
	void PostResize(unsigned int _windowWidth, unsigned int _windowHeight,
					Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV,
					Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthStencilDSV);
	void Render(std::shared_ptr<Camera> camera);
	void DrawPointLights();
	void DrawUI();
	void UpdateImGui(float deltaTime);
	void CreateGui();
	void UIProgram();
	void UICamera();
	void UILight(Light& light, int index);
	void UIEntity(GameEntity& entity, int index);
	void UITransform(Transform& transform, int parentIndex);
	void UIMaterial();
};

