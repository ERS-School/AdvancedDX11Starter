#ifndef __GGP2_LIGHTING__
#define __GGP2_LIGHTING__

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2

#define MAX_LIGHTS 128
#define MAX_SPECULAR_EXPONENT 256.0f

// The fresnel value for non-metals (dielectrics)
// Page 9: "F0 of nonmetals is now a constant 0.04"
// http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
static const float F0_NON_METAL = 0.04f;

// Minimum roughness for when spec distribution function denominator goes to zero
static const float MIN_ROUGHNESS = 0.0000001f; // 6 zeros after decimal

// Handy to have this as a constant
static const float PI = 3.14159265359f;






//Any kind of basic light:
struct Light
{
	int Type;
	float3 Direction;
	float Range;
	float3 Position;
	float Intensity;
	float3 Color;
	float SpotFalloff;
	float3 Padding; //having this sets up the struct to be used in arrays
    
};

// Struct representing a single vertex worth of data
// - This should match the vertex definition in our C++ code
// - By "match", I mean the size, order and number of members
// - The name of the struct itself is unimportant, but should be descriptive
// - Each variable must have a semantic, which defines its usage
/*struct VertexShaderInput
{
    // Data type
    //  |
    //  |   Name          Semantic
    //  |    |                |
    //  v    v                v
	float3 localPosition : POSITION; // XYZ position
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float2 uv : TEXCOORD;
};*/

// Struct representing the data we expect to receive from earlier pipeline stages
// - Should match the output of our corresponding vertex shader
// - The name of the struct itself is unimportant
// - The variable names don't have to match other shaders (just the semantics)
// - Each variable must have a semantic, which defines its usage
/*struct VertexToPixel
{
	// Data type
	//  |
	//  |   Name          Semantic
	//  |    |                |
	//  v    v                v
	float2 uv : TEXCOORD;
	float4 screenPosition : SV_POSITION;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 worldPosition : POSITION;

};*/

struct Sky_VertexToPixel
{
	float4 position : SV_POSITION;
	float3 sampleDir : DIRECTION;
};






float3 SampleAndUnpackNormalMap(Texture2D map, SamplerState samp, float2 uv)
{
	return map.Sample(samp, uv).rgb * 2.0f - 1.0f;
}

// Handle converting tangent-space normal map to world space normal
float3 NormalMapping(Texture2D map, SamplerState samp, float2 uv, float3 normal, float3 tangent)
{
	// Grab the normal from the map
	float3 normalFromMap = SampleAndUnpackNormalMap(map, samp, uv);

	// Gather the required vectors for converting the normal
	float3 N = normal;
	float3 T = normalize(tangent - N * dot(tangent, N));
	float3 B = cross(T, N);

	// Create the 3x3 matrix to convert from TANGENT-SPACE normals to WORLD-SPACE normals
	float3x3 TBN = float3x3(T, B, N);

	// Adjust the normal from the map and simply use the results
	return normalize(mul(normalFromMap, TBN));
}

float3 AmbientTerm(float3 ambColor, float3 objColor)
{
	return ambColor * objColor;
}

float NDotL(float3 norm, float3 lightDir)
{
	return saturate(dot(norm, -lightDir)); //saturate clamps between 0 and 1
											//take the negative of the light direction for correct result
}

float3 NormalVecToCam(float3 camPos, float3 worldPos)
{
	return normalize(camPos - worldPos);
}

float3 Reflection(float3 norm, float3 lightDir)
{
	return reflect(lightDir, norm); //here we DON'T negate light direction
}

float Attenuate(float3 worldPos, Light light)
{
	switch (light.Type)
	{
		case LIGHT_TYPE_DIRECTIONAL:
			return 1; //multiplying by 1 changes nothing. Dir lights don't attenuate
		case LIGHT_TYPE_POINT:
		default:
			float dist = distance(light.Position, worldPos);
			float att = saturate(1.0f - (dist * dist / (light.Range * light.Range)));
			return att * att;
	}
}

float3 DiffuseTerm(float3 norm, float3 worldPos, Light light)
{
	switch (light.Type)
	{
		case LIGHT_TYPE_DIRECTIONAL:
			return NDotL(norm, normalize(light.Direction)) * light.Color * light.Intensity;
		case LIGHT_TYPE_POINT:
		default:
			float3 dir = normalize(worldPos - light.Position); //"light direction"
			return NDotL(norm, dir) * light.Color * light.Intensity;
            
	}
}

// Simple specular calculation (aka Phong)
float3 SpecularTermPhong(float3 norm, float roughness, float3 worldPos, float3 camPos, Light light)
{
    
    // SPECULAR EXPONENT
	float specExponent = (1.0f - roughness) * MAX_SPECULAR_EXPONENT;
        //avoid making the exponent 0. Anything to the 0 power = 1, making whole object white
	if (specExponent == 0)
		return 0;
    
    // REFLECTION
	float3 refl = 0;
	switch (light.Type)
	{
		case LIGHT_TYPE_DIRECTIONAL:
			refl = Reflection(norm, light.Direction);
			break;
		case LIGHT_TYPE_POINT:
		default:
			float3 dir = normalize(worldPos - light.Position); //"light direction"
			refl = Reflection(norm, dir);
			break;
	}
    
    // VECTOR FROM PIXEL TO CAMERA
	float3 toCamera = NormalVecToCam(camPos, worldPos);
    
    // SPECULAR CALCULATION:
	float3 specularTerm = pow(saturate(dot(refl, toCamera)), specExponent); // The higher this number, the sharper the shine falloff is
																	        // shinier surfaces, higher number! (more perfect, less fuzzy reflection)
	specularTerm *= light.Color * light.Intensity; // apply properties of the light source

	return specularTerm;
}



// PBR FUNCTIONS ================

// Lambert diffuse BRDF - Same as the basic lighting diffuse calculation!
// - NOTE: this function assumes the vectors are already NORMALIZED!
float DiffusePBR
	(
	float3 normal, float3 dirToLight)
{
	return saturate(dot(normal, dirToLight));
}




// Calculates diffuse amount based on energy conservation
//
// diffuse - Diffuse amount
// specular - Specular color (including light color)
// metalness - surface metalness amount
//
// Metals should have an albedo of (0,0,0)...mostly
// See slide 65: http://blog.selfshadow.com/publications/s2014-shading-course/hoffman/s2014_pbs_physics_math_slides.pdf
float3 DiffuseEnergyConserve
	(
	float3 diffuse, float3 specular, float metalness)
{
	return diffuse * ((1 - saturate(specular)) * (1 - metalness));
}




// GGX (Trowbridge-Reitz)
//
// a - Roughness
// h - Half vector
// n - Normal
// 
// D(h, n) = a^2 / pi * ((n dot h)^2 * (a^2 - 1) + 1)^2
float SpecDistribution
	(
	float3 n, float3 h, float roughness)
{
	// Pre-calculations
	float NdotH = saturate(dot(n, h));
	float NdotH2 = NdotH * NdotH;
	float a = roughness * roughness;
	float a2 = max(a * a, MIN_ROUGHNESS); // Applied after remap!

	// ((n dot h)^2 * (a^2 - 1) + 1)
	float denomToSquare = NdotH2 * (a2 - 1) + 1;
	// Can go to zero if roughness is 0 and NdotH is 1; MIN_ROUGHNESS helps here

	// Final value
	return a2 / (PI * denomToSquare * denomToSquare);
}




// Fresnel term - Schlick approx.
// 
// v - View vector
// h - Half vector
// f0 - Value when l = n (full specular color)
//
// F(v,h,f0) = f0 + (1-f0)(1 - (v dot h))^5
float3 Fresnel
	(
	float3 v, float3 h, float3 f0)
{
	// Pre-calculations
	float VdotH = saturate(dot(v, h));

	// Final value
	return f0 + (1 - f0) * pow(1 - VdotH, 5);
}




// Geometric Shadowing - Schlick-GGX (based on Schlick-Beckmann)
// - k is remapped to a / 2, roughness remapped to (r+1)/2
//
// n - Normal
// v - View vector
//
// G(l,v)
float GeometricShadowing
	(
	float3 n, float3 v, float roughness)
{
	// End result of remapping:
	float k = pow(roughness + 1, 2) / 8.0f;
	float NdotV = saturate(dot(n, v));

	// Final value
	return NdotV / (NdotV * (1 - k) + k);
}



// Microfacet BRDF (Specular)
//
// f(l,v) = D(h)F(v,h)G(l,v,h) / 4(n dot l)(n dot v)
// - part of the denominator are canceled out by numerator (see below)
//
// D() - Spec Dist - Trowbridge-Reitz (GGX)
// F() - Fresnel - Schlick approx
// G() - Geometric Shadowing - Schlick-GGX
float3 MicrofacetBRDF
	(
	float3 n, float3 l, float3 v, float roughness, float3 specColor)
{
	// Other vectors
	float3 h = normalize(v + l);

	// Grab various functions
	float D = SpecDistribution(n, h, roughness);
	float3 F = Fresnel(v, h, specColor);
	float G = GeometricShadowing(n, v, roughness) * GeometricShadowing(n, l, roughness);

	// Final formula
	// Denominator dot products partially canceled by G()!
	// See page 16: http://blog.selfshadow.com/publications/s2012-shading-course/hoffman/s2012_pbs_physics_math_notes.pdf
	return (D * F * G) / (4 * max(dot(n, v), dot(n, l)));
}

float3 DirLightPBR(Light light, float3 norm, float3 worldPos, float3 camPos, float rough, float metal, float3 surfaceColor, float3 specColor)
{
    // We will need these
	float3 toLight = normalize(-light.Direction);
	float3 toCam = normalize(camPos - worldPos);
    
    // Calc light amounts
	float diffuse = DiffusePBR(norm, toLight);
	float3 spec = MicrofacetBRDF(norm, toLight, toCam, rough, specColor);
    
    // Conserve energy, just like real life
	float3 balancedDiffuse = DiffuseEnergyConserve(diffuse, spec, metal);
    
    // Combine that all with the light's properties
	return (balancedDiffuse * surfaceColor + spec) * light.Intensity * light.Color;

}

float3 PointLightPBR(Light light, float3 norm, float3 worldPos, float3 camPos, float rough, float metal, float3 surfaceColor, float3 specColor)
{
    // Calc light direction
	float3 toLight = normalize(light.Position - worldPos);
	float3 toCam = normalize(camPos - worldPos);
    
    // Calc light amounts
	float attenuate = Attenuate(worldPos, light);
	float diffuse = DiffusePBR(norm, toLight);
	float3 spec = MicrofacetBRDF(norm, toLight, toCam, rough, specColor);
    
    // Conserve energy like real life
	float3 balancedDiffuse = DiffuseEnergyConserve(diffuse, spec, metal);
    
    // Combine that all with the light's properties
	return (balancedDiffuse * surfaceColor + spec) * light.Intensity * light.Color;
}
#endif
