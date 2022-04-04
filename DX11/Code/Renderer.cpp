#include "Renderer.h"

#include "Input.h"
#include "imgui/imgui.h"
#include "Imgui/imgui_impl_dx11.h"
#include "Imgui/imgui_impl_win32.h"

#include <DirectXMath.h>

using namespace DirectX;

Renderer::Renderer(Microsoft::WRL::ComPtr<ID3D11Device> _device, Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context, Microsoft::WRL::ComPtr<IDXGISwapChain> _swapChain,
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV,
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthBufferDSV,
	unsigned int _windowWidth, unsigned int _windowHeight,
	std::shared_ptr<Sky> _sky,
	std::vector<std::shared_ptr<GameEntity>>& _entities,
	std::vector<Light>& _lights,
	std::shared_ptr<Mesh> _lightMesh,
	std::shared_ptr<SimpleVertexShader> _lightVS,
	std::shared_ptr<SimplePixelShader> _lightPS,
	unsigned int _activeLightCount,
	std::shared_ptr<DirectX::SpriteFont> _arial,
	std::shared_ptr<DirectX::SpriteBatch> _spriteBatch,
	std::shared_ptr<SimpleVertexShader> _fullscreenVS,
	std::shared_ptr<SimplePixelShader> _ssaoPS,
	std::shared_ptr<SimplePixelShader> _ssaoBlurPS,
	std::shared_ptr<SimplePixelShader> _ssaoCombinePS,
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _randomSRV,
	Microsoft::WRL::ComPtr<ID3D11SamplerState> _basicSamplerOptions,
	Microsoft::WRL::ComPtr<ID3D11SamplerState> _clampSamplerOptions)
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
	activeLightCount(_activeLightCount),
	arial(_arial),
	spriteBatch(_spriteBatch),
	drawDebugPointLights(false),
	fullscreenVS(_fullscreenVS),
	ssaoPS(_ssaoPS),
	ssaoBlurPS(_ssaoBlurPS),
	ssaoCombinePS(_ssaoCombinePS),
	randomSRV(_randomSRV),
	basicSamplerOptions(_basicSamplerOptions),
	clampSamplerOptions(_clampSamplerOptions)
{
	// Validate active light count
	activeLightCount = min(activeLightCount, MAX_LIGHTS);

	//Create MRTs
	PostResize(windowWidth, windowHeight, backBufferRTV, depthBufferDSV);

	// Set up the ssao offsets (count must match shader!)
	for (int i = 0; i < ARRAYSIZE(ssaoOffsets); i++)
	{
		ssaoOffsets[i] = XMFLOAT4(
			(float)rand() / RAND_MAX * 2 - 1,	// -1 to 1
			(float)rand() / RAND_MAX * 2 - 1,	// -1 to 1
			(float)rand() / RAND_MAX,			// 0 to 1
			0);

		XMVECTOR v = XMVector3Normalize(XMLoadFloat4(&ssaoOffsets[i]));

		// Scale up over the array
		float scale = (float)i / ARRAYSIZE(ssaoOffsets);
		XMVECTOR scaleVector = XMVectorLerp(
			XMVectorSet(0.1f, 0.1f, 0.1f, 1),
			XMVectorSet(1, 1, 1, 1),
			scale * scale);

		XMStoreFloat4(&ssaoOffsets[i], v * scaleVector);
	}
	ssaoRadius = 1.0f;
	ssaoSamples = 64;
	ssaoOutputOnly = 0;
}

void Renderer::PreResize()
{
	backBufferRTV.Reset();
	depthBufferDSV.Reset();
}

void Renderer::PostResize(
	unsigned int windowWidth,
	unsigned int windowHeight,
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRTV,
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthBufferDSV)
{
	this->windowWidth = windowWidth;
	this->windowHeight = windowHeight;
	this->backBufferRTV = backBufferRTV;
	this->depthBufferDSV = depthBufferDSV;

	// Release all of the renderer-specific render targets
	for (auto& rt : renderTargetSRVs) rt.Reset();
	for (auto& rt : renderTargetRTVs) rt.Reset();

	// Recreate using the new window size
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SCENE_COLORS_NO_AMBIENT], renderTargetSRVs[RenderTargetType::SCENE_COLORS_NO_AMBIENT]);
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SCENE_AMBIENT], renderTargetSRVs[RenderTargetType::SCENE_AMBIENT]);
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SCENE_NORMALS], renderTargetSRVs[RenderTargetType::SCENE_NORMALS]);
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SCENE_DEPTHS], renderTargetSRVs[RenderTargetType::SCENE_DEPTHS], DXGI_FORMAT_R32_FLOAT);
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SSAO_RESULTS], renderTargetSRVs[RenderTargetType::SSAO_RESULTS]);
	CreateRenderTarget(windowWidth, windowHeight, renderTargetRTVs[RenderTargetType::SSAO_BLUR], renderTargetSRVs[RenderTargetType::SSAO_BLUR]);
}

void Renderer::Render(std::shared_ptr<Camera> camera)
{
	// update what's stored in the Renderer
	this->camera = camera;

	// Background color for clearing
	const float color[4] = { 0, 0, 0, 1 };


	
	// Clear the render target and depth buffer (erases what's on the screen)
	//  - Do this ONCE PER FRAME
	//  - At the beginning of Draw (before drawing *anything*)
	context->ClearRenderTargetView(backBufferRTV.Get(), color);
	context->ClearDepthStencilView(
		depthBufferDSV.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0);
	// Clear render targets
	for (auto& rt : renderTargetRTVs) context->ClearRenderTargetView(rt.Get(), color);
	const float depth[4] = { 1,0,0,0 };
	context->ClearRenderTargetView(renderTargetRTVs[SCENE_DEPTHS].Get(), depth);

	const int numTargets = 4;
	ID3D11RenderTargetView* targets[numTargets] = {};
	targets[0] = renderTargetRTVs[RenderTargetType::SCENE_COLORS_NO_AMBIENT].Get();
	targets[1] = renderTargetRTVs[RenderTargetType::SCENE_AMBIENT].Get();
	targets[2] = renderTargetRTVs[RenderTargetType::SCENE_NORMALS].Get();
	targets[3] = renderTargetRTVs[RenderTargetType::SCENE_DEPTHS].Get();
	context->OMSetRenderTargets(numTargets, targets, depthBufferDSV.Get());

	// Draw all of the entities
	for (auto& e : entities)
	{
		// Set the "per frame" data
		// Note that this should literally be set once PER FRAME, before
		// the draw loop, but we're currently setting it per entity since 
		// we are just using whichever shader the current entity has.  
		// Inefficient!!!
		std::shared_ptr<SimplePixelShader> ps = e->GetMaterial()->GetPixelShader();
		ps->SetData("lights", (void*)(&lights[0]), sizeof(Light) * activeLightCount);
		ps->SetInt("lightCount", activeLightCount);
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


	// ---- SSAO ---- //
	fullscreenVS->SetShader();
	// Render the SSAO results
	{
		// Set up ssao render pass
		targets[0] = renderTargetRTVs[RenderTargetType::SSAO_RESULTS].Get();
		targets[1] = 0;
		targets[2] = 0;
		targets[3] = 0;
		context->OMSetRenderTargets(numTargets, targets, 0);

		ssaoPS->SetShader();

		// Calculate the inverse of the camera matrices
		XMFLOAT4X4 invView, invProj, view = camera->GetView(), proj = camera->GetProjection();
		XMStoreFloat4x4(&invView, XMMatrixInverse(0, XMLoadFloat4x4(&view)));
		XMStoreFloat4x4(&invProj, XMMatrixInverse(0, XMLoadFloat4x4(&proj)));
		ssaoPS->SetMatrix4x4("invViewMatrix", invView); // ??? not in shader
		ssaoPS->SetMatrix4x4("invProjMatrix", invProj);
		ssaoPS->SetMatrix4x4("viewMatrix", view);
		ssaoPS->SetMatrix4x4("projectionMatrix", proj);
		ssaoPS->SetData("offsets", ssaoOffsets, sizeof(XMFLOAT4) * ARRAYSIZE(ssaoOffsets));
		ssaoPS->SetFloat("ssaoRadius", ssaoRadius);
		ssaoPS->SetInt("ssaoSamples", ssaoSamples);
		ssaoPS->SetFloat2("randomTextureScreenScale", XMFLOAT2(windowWidth / 4.0f, windowHeight / 4.0f));
		ssaoPS->SetSamplerState("BasicSampler", basicSamplerOptions); // ??? do we need these?
		ssaoPS->SetSamplerState("ClampSampler", clampSamplerOptions);
		ssaoPS->CopyAllBufferData();

		ssaoPS->SetShaderResourceView("Normals", renderTargetSRVs[RenderTargetType::SCENE_NORMALS]);
		ssaoPS->SetShaderResourceView("Depths", renderTargetSRVs[RenderTargetType::SCENE_DEPTHS]);
		ssaoPS->SetShaderResourceView("Random", randomSRV);


		context->Draw(3, 0);
	}
	// SSAO Blur step
	{
		// Set up blur (assuming all other targets are null here)
		targets[0] = renderTargetRTVs[RenderTargetType::SSAO_BLUR].Get();
		context->OMSetRenderTargets(1, targets, 0);

		ssaoBlurPS->SetShader();
		ssaoBlurPS->SetShaderResourceView("SSAO", renderTargetSRVs[RenderTargetType::SSAO_RESULTS]);
		ssaoBlurPS->SetFloat2("pixelSize", XMFLOAT2(1.0f / windowWidth, 1.0f / windowHeight));

		ssaoBlurPS->SetSamplerState("BasicSampler", basicSamplerOptions); // ??? do we need these?
		ssaoBlurPS->SetSamplerState("ClampSampler", clampSamplerOptions);

		ssaoBlurPS->CopyAllBufferData();
		context->Draw(3, 0);
	}

	// Final combine
	{
		// Re-enable back buffer (assuming all other targets are null here)
		targets[0] = backBufferRTV.Get();
		context->OMSetRenderTargets(1, targets, 0);

		ssaoCombinePS->SetShader();
		ssaoCombinePS->SetShaderResourceView("SceneColorsNoAmbient", renderTargetSRVs[RenderTargetType::SCENE_COLORS_NO_AMBIENT]);
		ssaoCombinePS->SetShaderResourceView("Ambient", renderTargetSRVs[RenderTargetType::SCENE_AMBIENT]);
		ssaoCombinePS->SetShaderResourceView("SSAOBlur", renderTargetSRVs[RenderTargetType::SSAO_BLUR]);
		ssaoCombinePS->SetInt("ssaoEnabled", ssaoEnabled);
		ssaoCombinePS->SetInt("ssaoOutputOnly", ssaoOutputOnly);
		ssaoCombinePS->SetFloat2("pixelSize", XMFLOAT2(1.0f / windowWidth, 1.0f / windowHeight));

		ssaoCombinePS->SetSamplerState("BasicSampler", basicSamplerOptions); // ??? do we need these?
		ssaoCombinePS->SetSamplerState("ClampSampler", clampSamplerOptions);

		ssaoCombinePS->CopyAllBufferData();
		context->Draw(3, 0);
	}

	// Draw some UI
	DrawUI();


	// Present the back buffer to the user
	//  - Puts the final frame we're drawing into the window so the user can see it
	//  - Do this exactly ONCE PER FRAME (always at the very end of the frame)
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // draw ImGui last so it appears on top of everything

	// Present and re-bind the RTV
	swapChain->Present(0, 0);
	context->OMSetRenderTargets(1, backBufferRTV.GetAddressOf(), depthBufferDSV.Get());

	// Unbind all SRVs at the end of the frame so they're not still bound for input
	// when we begin the MRTs of the next frame
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	context->PSSetShaderResources(0, 16, nullSRVs);
}

unsigned int Renderer::GetActiveLightCount() { return activeLightCount; }
void Renderer::SetActiveLightCount(unsigned int count) { activeLightCount = min(count, MAX_LIGHTS); }


void Renderer::CreateRenderTarget(
	unsigned int width,
	unsigned int height,
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv,
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,
	DXGI_FORMAT colorFormat)
{
	// Make the texture
	Microsoft::WRL::ComPtr<ID3D11Texture2D> rtTexture;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; // Need both!
	texDesc.Format = colorFormat;
	texDesc.MipLevels = 1; // Usually no mip chain needed for render targets
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1; // Can't be zero
	device->CreateTexture2D(&texDesc, 0, rtTexture.GetAddressOf());

	// Make the render target view
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; // This points to a Texture2D
	rtvDesc.Texture2D.MipSlice = 0;                             // Which mip are we rendering into?
	rtvDesc.Format = texDesc.Format;                // Same format as texture
	device->CreateRenderTargetView(rtTexture.Get(), &rtvDesc, rtv.GetAddressOf());

	// Create the shader resource view using default options 
	device->CreateShaderResourceView(
		rtTexture.Get(),     // Texture resource itself
		0,                   // Null description = default SRV options
		srv.GetAddressOf()); // ComPtr<ID3D11ShaderResourceView>
}


void Renderer::DrawPointLights()
{
	// Turn on these shaders
	lightVS->SetShader();
	lightPS->SetShader();

	// Set up vertex shader
	lightVS->SetMatrix4x4("view", camera->GetView());
	lightVS->SetMatrix4x4("projection", camera->GetProjection());

	for (int i = 0; i < activeLightCount; i++)
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
		int lightCount = activeLightCount;
		if (ImGui::SliderInt("Light Count", &lightCount, 0, MAX_LIGHTS))
			SetActiveLightCount((unsigned int)lightCount);
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
	if (ImGui::CollapsingHeader("MRTs"))
	{
		ImVec2 size = ImGui::GetItemRectSize();
		float rtHeight = size.x * ((float)windowHeight / windowWidth);

		for (int i = 0; i < RenderTargetType::RENDER_TARGET_TYPE_COUNT; i++)
		{
			ImageWithHover(renderTargetSRVs[((RenderTargetType)i)].Get(), ImVec2(size.x, rtHeight));
		}

		ImageWithHover(randomSRV.Get(), ImVec2(32, 32));
	}
	// SSAO Options
	if (ImGui::CollapsingHeader("SSAO Options"))
	{
		ImVec2 size = ImGui::GetItemRectSize();
		float rtHeight = size.x * ((float)windowHeight / windowWidth);

		bool ssao = GetSSAOEnabled();
		if (ImGui::Button(ssao ? "SSAO Enabled" : "SSAO Disabled"))
			SetSSAOEnabled(!ssao);

		ImGui::SameLine();
		bool ssaoOnly = GetSSAOOutputOnly();
		if (ImGui::Button("SSAO Output Only"))
			SetSSAOOutputOnly(!ssaoOnly);

		int ssaoSamples = GetSSAOSamples();
		if (ImGui::SliderInt("SSAO Samples", &ssaoSamples, 1, 64))
			SetSSAOSamples(ssaoSamples);

		float ssaoRadius = GetSSAORadius();
		if (ImGui::SliderFloat("SSAO Sample Radius", &ssaoRadius, 0.0f, 2.0f))
			SetSSAORadius(ssaoRadius);

	}
	ImGui::End();
}

void Renderer::ImageWithHover(ImTextureID user_texture_id, const ImVec2& size)
{
	// Draw the image
	ImGui::Image(user_texture_id, size);

	// Check for hover
	if (ImGui::IsItemHovered())
	{
		// Zoom amount and aspect of the image
		float zoom = 0.03f;
		float aspect = (float)size.x / size.y;

		// Get the coords of the image
		ImVec2 topLeft = ImGui::GetItemRectMin();
		ImVec2 bottomRight = ImGui::GetItemRectMax();

		// Get the mouse pos as a percent across the image, clamping near the edge
		ImVec2 mousePosGlobal = ImGui::GetMousePos();
		ImVec2 mousePos = ImVec2(mousePosGlobal.x - topLeft.x, mousePosGlobal.y - topLeft.y);
		ImVec2 uvPercent = ImVec2(mousePos.x / size.x, mousePos.y / size.y);

		uvPercent.x = max(uvPercent.x, zoom / 2);
		uvPercent.x = min(uvPercent.x, 1 - zoom / 2);
		uvPercent.y = max(uvPercent.y, zoom / 2 * aspect);
		uvPercent.y = min(uvPercent.y, 1 - zoom / 2 * aspect);

		// Figure out the uv coords for the zoomed image
		ImVec2 uvTL = ImVec2(uvPercent.x - zoom / 2, uvPercent.y - zoom / 2 * aspect);
		ImVec2 uvBR = ImVec2(uvPercent.x + zoom / 2, uvPercent.y + zoom / 2 * aspect);

		// Draw a floating box with a zoomed view of the image
		ImGui::BeginTooltip();
		ImGui::Image(user_texture_id, ImVec2(256, 256), uvTL, uvBR);
		ImGui::EndTooltip();
	}
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

void Renderer::SetSSAOEnabled(bool enabled) { ssaoEnabled = enabled; }
bool Renderer::GetSSAOEnabled() { return ssaoEnabled; }

void Renderer::SetSSAORadius(float radius) { ssaoRadius = radius; }
float Renderer::GetSSAORadius() { return ssaoRadius; }

void Renderer::SetSSAOSamples(int samples) { ssaoSamples = max(0, min(samples, ARRAYSIZE(ssaoOffsets))); }
int Renderer::GetSSAOSamples() { return ssaoSamples; }

void Renderer::SetSSAOOutputOnly(bool ssaoOnly) { ssaoOutputOnly = ssaoOnly; }
bool Renderer::GetSSAOOutputOnly() { return ssaoOutputOnly; }
