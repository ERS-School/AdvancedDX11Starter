#include "Renderer.h"

#include "Input.h"
#include "imgui/imgui.h"
#include "Imgui/imgui_impl_dx11.h"
#include "Imgui/imgui_impl_win32.h"

#include <DirectXMath.h>

using namespace DirectX;

Renderer::Renderer(Microsoft::WRL::ComPtr<ID3D11Device> _device, Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context,
	Microsoft::WRL::ComPtr<IDXGISwapChain> _swapChain,
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV,
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthBufferDSV,
	unsigned _windowWidth, unsigned _windowHeight,
	std::shared_ptr<Sky> _sky,
	std::vector<std::shared_ptr<GameEntity>>& _entities,
	std::vector<Light>& _lights,
	std::shared_ptr<Mesh> _lightMesh,
	std::shared_ptr<SimpleVertexShader> _lightVS,
	std::shared_ptr<SimplePixelShader> _lightPS,
	std::shared_ptr<DirectX::SpriteFont> _arial,
	std::shared_ptr<DirectX::SpriteBatch> _spriteBatch)
	:
	device(_device),
	context(_context),
	swapChain(_swapChain),
	backBufferRTV(_backBufferRTV),
	depthBufferDSV(_depthBufferDSV),
	windowWidth(_windowWidth),
	windowHeight(_windowHeight),
	sky(_sky),
	entities(_entities),
	lights(_lights),
	lightMesh(_lightMesh),
	lightVS(_lightVS),
	lightPS(_lightPS),
	arial(_arial),
	spriteBatch(_spriteBatch),
	drawDebugPointLights(false)
{
}

void Renderer::PreResize()
{
	backBufferRTV.Reset();
	depthBufferDSV.Reset();
}

void Renderer::PostResize(unsigned int _windowWidth, unsigned int _windowHeight, Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV, Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthStencilDSV)
{
	windowWidth = _windowWidth;
	windowHeight = _windowHeight;
	backBufferRTV = _backBufferRTV;
	depthBufferDSV = _depthStencilDSV;
}

void Renderer::Render(std::shared_ptr<Camera> camera)
{
	// update what's stored in the Renderer
	this->camera = camera;

	// Background color for clearing
	const float color[4] = { 0, 0, 0, 1 };


	const int lightCount = lights.size();
	
	// Clear the render target and depth buffer (erases what's on the screen)
	//  - Do this ONCE PER FRAME
	//  - At the beginning of Draw (before drawing *anything*)
	context->ClearRenderTargetView(backBufferRTV.Get(), color);
	context->ClearDepthStencilView(
		depthBufferDSV.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0);


	// Draw all of the entities
	for (auto& e : entities)
	{
		// Set the "per frame" data
		// Note that this should literally be set once PER FRAME, before
		// the draw loop, but we're currently setting it per entity since 
		// we are just using whichever shader the current entity has.  
		// Inefficient!!!
		std::shared_ptr<SimplePixelShader> ps = e->GetMaterial()->GetPixelShader();
		ps->SetData("lights", (void*)(&lights[0]), sizeof(Light) * lightCount);
		ps->SetInt("lightCount", lightCount);
		ps->SetFloat3("cameraPosition", camera->GetTransform()->GetPosition());
		ps->SetInt("SpecIBLTotalMipLevels", sky->GetNumIBLMipLevels());

		ps->CopyBufferData("perFrame");

		// Draw the entity
		e->Draw(context, camera);
	}

	// Draw the light sources
	DrawPointLights();

	// Draw the sky
	sky->Draw(camera);

	// Draw some UI
	DrawUI();


	// Present the back buffer to the user
	//  - Puts the final frame we're drawing into the window so the user can see it
	//  - Do this exactly ONCE PER FRAME (always at the very end of the frame)
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // draw ImGui last so it appears on top of everything
	swapChain->Present(0, 0);

	// Due to the usage of a more sophisticated swap chain,
	// the render target must be re-bound after every call to Present()
	context->OMSetRenderTargets(1, backBufferRTV.GetAddressOf(), depthBufferDSV.Get());
}

void Renderer::DrawPointLights()
{
	const int lightCount = lights.size();

	// Turn on these shaders
	lightVS->SetShader();
	lightPS->SetShader();

	// Set up vertex shader
	lightVS->SetMatrix4x4("view", camera->GetView());
	lightVS->SetMatrix4x4("projection", camera->GetProjection());

	for (int i = 0; i < lightCount; i++)
	{
		Light light = lights[i];

		// Only drawing points, so skip others
		if (light.Type != LIGHT_TYPE_POINT)
			continue;

		// Calc quick scale based on range
		float scale = light.Range / 20.0f;

		// Make the transform for this light
		XMMATRIX rotMat = XMMatrixIdentity();
		XMMATRIX scaleMat = XMMatrixScaling(scale, scale, scale);
		XMMATRIX transMat = XMMatrixTranslation(light.Position.x, light.Position.y, light.Position.z);
		XMMATRIX worldMat = scaleMat * rotMat * transMat;

		XMFLOAT4X4 world;
		XMFLOAT4X4 worldInvTrans;
		XMStoreFloat4x4(&world, worldMat);
		XMStoreFloat4x4(&worldInvTrans, XMMatrixInverse(0, XMMatrixTranspose(worldMat)));

		// Set up the world matrix for this light
		lightVS->SetMatrix4x4("world", world);
		lightVS->SetMatrix4x4("worldInverseTranspose", worldInvTrans);

		// Copy data
		lightVS->CopyAllBufferData();

		if (drawDebugPointLights)
		{
			// Set up the pixel shader data
			XMFLOAT3 finalColor = light.Color;
			finalColor.x *= light.Intensity;
			finalColor.y *= light.Intensity;
			finalColor.z *= light.Intensity;
			lightPS->SetFloat3("Color", finalColor);

			// Copy data & draw
			lightPS->CopyAllBufferData();
			lightMesh->SetBuffersAndDraw(context);
		}
	}
}

void Renderer::DrawUI()
{
	spriteBatch->Begin();

	// Basic controls
	float h = 10.0f;
	arial->DrawString(spriteBatch.get(), L"Controls:", XMVectorSet(10, h, 0, 0));
	arial->DrawString(spriteBatch.get(), L" (WASD, X, Space) Move camera", XMVectorSet(10, h + 20, 0, 0));
	arial->DrawString(spriteBatch.get(), L" (Left Click & Drag) Rotate camera", XMVectorSet(10, h + 40, 0, 0));
	arial->DrawString(spriteBatch.get(), L" (Left Shift) Hold to speed up camera", XMVectorSet(10, h + 60, 0, 0));
	arial->DrawString(spriteBatch.get(), L" (Left Ctrl) Hold to slow down camera", XMVectorSet(10, h + 80, 0, 0));
	arial->DrawString(spriteBatch.get(), L" (TAB) Randomize lights", XMVectorSet(10, h + 100, 0, 0));

	// Current "scene" info
	h = 150;
	arial->DrawString(spriteBatch.get(), L"Scene Details:", XMVectorSet(10, h, 0, 0));
	arial->DrawString(spriteBatch.get(), L" Top: PBR materials", XMVectorSet(10, h + 20, 0, 0));
	arial->DrawString(spriteBatch.get(), L" Bottom: Non-PBR materials", XMVectorSet(10, h + 40, 0, 0));

	spriteBatch->End();

	// Reset render states, since sprite batch changes these!
	context->OMSetBlendState(0, 0, 0xFFFFFFFF);
	context->OMSetDepthStencilState(0, 0);

}

void Renderer::UpdateImGui(float deltaTime)
{
	Input& input = Input::GetInstance();

	// Reset input manager's gui state so we don't taint our own input
	input.SetGuiKeyboardCapture(false);
	input.SetGuiMouseCapture(false);

	// Set io info
	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = deltaTime;
	io.DisplaySize.x = (float)this->windowWidth;
	io.DisplaySize.y = (float)this->windowHeight;
	io.KeyCtrl = input.KeyDown(VK_CONTROL);
	io.KeyShift = input.KeyDown(VK_SHIFT);
	io.KeyAlt = input.KeyDown(VK_MENU);
	io.MousePos.x = (float)input.GetMouseX();
	io.MousePos.y = (float)input.GetMouseY();
	io.MouseDown[0] = input.MouseLeftDown();
	io.MouseDown[1] = input.MouseRightDown();
	io.MouseDown[2] = input.MouseMiddleDown();
	io.MouseWheel = input.GetMouseWheel();
	input.GetKeyArray(io.KeysDown, 256);

	// Reset the frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Determine newe input capture
	input.SetGuiKeyboardCapture(io.WantCaptureKeyboard);
	input.SetGuiMouseCapture(io.WantCaptureMouse);

	// Show the demo window OR project debug gui
	//ImGui::ShowDemoWindow();
	CreateGui();
}

void Renderer::CreateGui()
{
	int lightCount = lights.size();

	ImGui::Begin("Debug");
	if (ImGui::CollapsingHeader("Program Stats"))
	{
		UIProgram();
	}
	if (ImGui::CollapsingHeader("Camera"))
	{
		UICamera();
	}
	if (ImGui::CollapsingHeader("Lights"))
	{
		ImGui::Checkbox("Draw Point Lights", &drawDebugPointLights);
		ImGui::SliderInt("Num Lights", &lightCount, 0, MAX_LIGHTS);
		//if using the slider to increase # of lights, make up the difference and add them to the vector, 
		while (lightCount >= lights.size())
		{
			Light light = {};
			lights.push_back(light);
		}
		for (int i = 0; i < lightCount; i++)
		{
			UILight(lights[i], i);
		}
	}
	if (ImGui::CollapsingHeader("Entities"))
	{
		for (int i = 0; i < entities.size(); i++)
		{
			UIEntity(*entities[i], i);
		}
	}
	if (ImGui::CollapsingHeader("BRDF Look-Up Texture"))
	{
		ImGui::Image(sky->GetIBLBRDFLookUpTexture().Get(), ImVec2(128, 128));
	}
	ImGui::End();
}

void Renderer::UIProgram()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("FPS: %.2f \nWidth: %d | Height: %d", io.Framerate, windowWidth, windowHeight);
}

void Renderer::UICamera()
{
	UITransform(*camera->GetTransform(), -1);
}

void Renderer::UILight(Light& light, int index)
{
	std::string indexStr = std::to_string(index);
	std::string nodeName = "Light " + indexStr;

	if (ImGui::TreeNode(nodeName.c_str()))
	{
		// Type
		std::string radioDirID = "Directional##" + indexStr;
		std::string radioPointID = "Point##" + indexStr;
		std::string radioSpotID = "Spot##" + indexStr;

		if (ImGui::RadioButton(radioDirID.c_str(), light.Type == LIGHT_TYPE_DIRECTIONAL))
		{
			light.Type = LIGHT_TYPE_DIRECTIONAL;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton(radioPointID.c_str(), light.Type == LIGHT_TYPE_POINT))
		{
			light.Type = LIGHT_TYPE_POINT;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton(radioSpotID.c_str(), light.Type == LIGHT_TYPE_SPOT))
		{
			light.Type = LIGHT_TYPE_SPOT;
		}

		if (light.Type == LIGHT_TYPE_POINT || light.Type == LIGHT_TYPE_SPOT)
		{
			// Position	
			std::string posID = "Position##" + indexStr;
			ImGui::Text("Position: ");
			ImGui::DragFloat3(posID.c_str(), &light.Position.x, 0.10f);

			// Range
			std::string rangeID = "Range##" + indexStr;
			ImGui::Text("Range: ");
			ImGui::SliderFloat(rangeID.c_str(), &light.Range, 0.10f, 100.00f);
		}

		if (light.Type == LIGHT_TYPE_SPOT)
		{
			std::string spotFalloffID = "Spot Falloff##" + indexStr;
			ImGui::Text("Spot Falloff: ");
			ImGui::SliderFloat(spotFalloffID.c_str(), &light.SpotFalloff, 0.10f, 128.00f);
		}

		// Color
		ImGui::Text("Color: ");
		std::string colorID = "Color##" + indexStr;
		ImGui::ColorEdit3(colorID.c_str(), &light.Color.x);

		// Intensity
		std::string intensityID = "Intensity##" + indexStr;
		ImGui::Text("Intensity: ");
		ImGui::SliderFloat(intensityID.c_str(), &light.Intensity, 0.10f, 10.00f);
		ImGui::TreePop();
	}
}

void Renderer::UIEntity(GameEntity& entity, int index)
{
	UITransform(*entity.GetTransform(), index);
	UIMaterial();
}

void Renderer::UITransform(Transform& transform, int parentIndex)
{
	std::string uid = std::to_string(parentIndex);

	std::string transformPosID = "TransformPos##" + uid;
	std::string transformRotID = "TransformRot##" + uid;
	std::string transformScaleID = "TransformScale##" + uid;
	ImGui::Text(uid.c_str());
	XMFLOAT3 tPos = transform.GetPosition();
	if (ImGui::InputFloat3(transformPosID.c_str(), &tPos.x))
	{
		transform.SetPosition(tPos.x, tPos.y, tPos.z);
	}
	XMFLOAT3 tRot = transform.GetPitchYawRoll();
	if (ImGui::InputFloat3(transformRotID.c_str(), &tRot.x))
	{
		transform.SetPosition(tRot.x, tRot.y, tRot.z);
	}
	XMFLOAT3 tScale = transform.GetScale();
	if (ImGui::InputFloat3(transformScaleID.c_str(), &tScale.x))
	{
		transform.SetPosition(tScale.x, tScale.y, tScale.z);
	}
}

void Renderer::UIMaterial() {}