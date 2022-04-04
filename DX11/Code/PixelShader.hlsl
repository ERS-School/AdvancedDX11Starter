
#include "Lighting.hlsli"

// How many lights could we handle?
#define MAX_LIGHTS 128

// Data that can change per material
cbuffer perMaterial : register(b0)
{
	// Surface color
	float3 colorTint;

	// UV adjustments
	float2 uvScale;
	float2 uvOffset;
};

// Data that only changes once per frame
cbuffer perFrame : register(b1)
{
	// An array of light data
	Light lights[MAX_LIGHTS];

	// The amount of lights THIS FRAME
	int lightCount;

	// Needed for specular (reflection) calculation
	float3 cameraPosition;
};


// Defines the input to this pixel shader
// - Should match the output of our corresponding vertex shader
struct VertexToPixel
{
	float4 screenPosition	: SV_POSITION;
	float2 uv				: TEXCOORD;
	float3 normal			: NORMAL;
	float3 tangent			: TANGENT;
	float3 worldPos			: POSITION; // The world position of this PIXEL
};

struct PS_Output
{
	float4 colorNoAmbient : SV_TARGET0;
	float4 ambientColor : SV_TARGET1;
	float4 normals : SV_TARGET2;
	float depths : SV_TARGET3;
};

// Texture-related variables
Texture2D Albedo			: register(t0);
Texture2D NormalMap			: register(t1);
Texture2D RoughnessMap		: register(t2);
SamplerState BasicSampler		: register(s0);


// Entry point for this pixel shader
PS_Output main(VertexToPixel input) : SV_TARGET
{
	// Always re-normalize interpolated direction vectors
	input.normal = normalize(input.normal);
	input.tangent = normalize(input.tangent);

	// Normal mapping
	input.normal = NormalMapping(NormalMap, BasicSampler, input.uv, input.normal, input.tangent);
	
	// Treating roughness as a pseduo-spec map here, so applying it as
	// a modifier to the overall shininess value of the material
	float roughness = RoughnessMap.Sample(BasicSampler, input.uv).r;
	float specPower = max(256.0f * (1.0f - roughness), 0.01f); // Ensure we never hit 0
	
	// Gamma correct the texture back to linear space and apply the color tint
	float4 surfaceColor = Albedo.Sample(BasicSampler, input.uv);
	surfaceColor.rgb = pow(surfaceColor.rgb, 2.2);// * Color.rgb;

	// Total color for this pixel
	float3 totalDirectLight = float3(0, 0, 0);

	// Loop through all lights this frame
	for (int i = 0; i < lightCount; i++)
	{
		// Which kind of light?
		switch (lights[i].Type)
		{
			case LIGHT_TYPE_DIRECTIONAL:
				totalDirectLight += DirLight(lights[i], input.normal, input.worldPos, cameraPosition, specPower, surfaceColor.rgb);
				break;

			case LIGHT_TYPE_POINT:
				totalDirectLight += PointLight(lights[i], input.normal, input.worldPos, cameraPosition, specPower, surfaceColor.rgb);
				break;

			case LIGHT_TYPE_SPOT:
				totalDirectLight += SpotLight(lights[i], input.normal, input.worldPos, cameraPosition, specPower, surfaceColor.rgb);
				break;
		}
	}

	// Handle ambient
	float3 ambient = surfaceColor.rgb * float3(0.2f, 0.2f, 0.2f);

	// Multiple render target output
	PS_Output output;
	output.colorNoAmbient = float4(totalDirectLight, 1); // No gamma correction yet!
	output.ambientColor = float4(ambient, 1);
	output.normals = float4(input.normal * 0.5f + 0.5f, 1);
	output.depths = input.screenPosition.z;
	return output;
}