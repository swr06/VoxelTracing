#version 330 core

#define PCF_COUNT 6
#define SMOOTH_SHADOW_SAMPLING
#define PI 3.14159265359

layout (location = 0) out vec3 o_Color;

in vec2 v_TexCoords;
in vec3 v_RayDirection;

uniform sampler2D u_DiffuseTexture;
uniform sampler2D u_NormalTexture;
uniform sampler2D u_InitialTracePositionTexture;
uniform sampler2D u_DataTexture;
uniform sampler2D u_ShadowTexture;
uniform sampler2DArray u_BlockAlbedoTextures;
uniform sampler2DArray u_BlockNormalTextures;
uniform sampler2DArray u_BlockPBRTextures;
uniform sampler2DArray u_BlueNoiseTextures;
uniform samplerCube u_Skybox;

uniform vec3 u_SunDirection;
uniform vec3 u_MoonDirection;

uniform mat4 u_ShadowView;
uniform mat4 u_ShadowProjection;


uniform vec3 u_ViewerPosition;


vec4 textureBicubic(sampler2D sampler, vec2 texCoords);
vec3 CalculateDirectionalLight(vec3 world_pos, vec3 light_dir, vec3 radiance, vec3 albedo, vec3 normal, vec3 pbr, float shadow);
void CalculateVectors(vec3 world_pos, in vec3 normal, out vec3 tangent, out vec3 bitangent, out vec2 uv);

float GetLuminance(vec3 color) {
	return dot(color, vec3(0.299, 0.587, 0.114));
}

//Due to low sample count we "tonemap" the inputs to preserve colors and smoother edges
vec3 WeightedSample(sampler2D colorTex, vec2 texcoord)
{
	vec3 wsample = texture(colorTex,texcoord).rgb * 1.0f;
	return wsample / (1.0f + GetLuminance(wsample));
}

vec3 smoothfilter(in sampler2D tex, in vec2 uv)
{
	vec2 textureResolution = textureSize(tex, 0);
	uv = uv*textureResolution + 0.5;
	vec2 iuv = floor( uv );
	vec2 fuv = fract( uv );
	uv = iuv + fuv*fuv*fuv*(fuv*(fuv*6.0-15.0)+10.0);
	uv = (uv - 0.5)/textureResolution;
	return WeightedSample( tex, uv);
}

vec3 sharpen(in sampler2D tex, in vec2 coords) 
{
	vec2 renderSize = textureSize(tex, 0);
	float dx = 1.0 / renderSize.x;
	float dy = 1.0 / renderSize.y;
	vec3 sum = vec3(0.0);
	sum += -1. * smoothfilter(tex, coords + vec2( -1.0 * dx , 0.0 * dy));
	sum += -1. * smoothfilter(tex, coords + vec2( 0.0 * dx , -1.0 * dy));
	sum += 5. * smoothfilter(tex, coords + vec2( 0.0 * dx , 0.0 * dy));
	sum += -1. * smoothfilter(tex, coords + vec2( 0.0 * dx , 1.0 * dy));
	sum += -1. * smoothfilter(tex, coords + vec2( 1.0 * dx , 0.0 * dy));
	return sum;
}

vec4 ClampedTexture(sampler2D tex, vec2 txc)
{
    return texture(tex, clamp(txc, 0.0f, 1.0f));
}

vec3 NeighbourhoodClamping(vec3 tempColor, sampler2D tex, vec2 txc) 
{
	vec2 neighbourhoodOffsets[8] = vec2[8]
	(
		vec2(-1.0, -1.0),
		vec2( 0.0, -1.0),
		vec2( 1.0, -1.0),
		vec2(-1.0,  0.0),
		vec2( 1.0,  0.0),
		vec2(-1.0,  1.0),
		vec2( 0.0,  1.0),
		vec2( 1.0,  1.0)
	);

	vec3 minclr = vec3(0.0f), maxclr = vec3(1.0);
    vec2 View = 1.0f / textureSize(tex, 0);

	for(int i = 0; i < 8; i++) 
	{
		vec2 offset = neighbourhoodOffsets[i] * View;
		vec3 clr = texture(tex, txc + offset, 0.0).rgb;
		minclr = min(minclr, clr);
		maxclr = max(maxclr, clr);
	}

	return clamp(tempColor, minclr, maxclr);
}

vec3 BilateralUpsample(sampler2D tex, vec2 txc, vec3 base_normal, float base_depth)
{
    const vec2 Kernel[4] = vec2[](
        vec2(0.0f, 1.0f),
        vec2(1.0f, 0.0f),
        vec2(-1.0f, 0.0f),
        vec2(0.0, -1.0f)
    );

    vec2 texel_size = 1.0f / textureSize(tex, 0);

    vec3 color = vec3(0.0f, 0.0f, 0.0f);
    float weight_sum;

    for (int i = 0; i < 4; i++) 
    {
        vec3 sampled_normal = texture(u_NormalTexture, txc + Kernel[i] * texel_size).xyz;
        float nweight = pow(abs(dot(sampled_normal, base_normal)), 32);

        float sampled_depth = texture(u_InitialTracePositionTexture, txc + Kernel[i] * texel_size).z; 
        float dweight = 1.0f / (abs(base_depth - sampled_depth) + 0.001f);

        float computed_weight = nweight * dweight;
        color.rgb += texture(tex, txc + Kernel[i] * texel_size).rgb * computed_weight;
        weight_sum += computed_weight;
    }

    color /= max(weight_sum, 0.2f);
    color = clamp(color, texture(tex, txc).rgb * 0.4f, vec3(1.0f));
    return color;
}

const vec2 PoissonDisk[32] = vec2[]
(
    vec2(-0.613392, 0.617481),  vec2(0.751946, 0.453352),
    vec2(0.170019, -0.040254),  vec2(0.078707, -0.715323),
    vec2(-0.299417, 0.791925),  vec2(-0.075838, -0.529344),
    vec2(0.645680, 0.493210),   vec2(0.724479, -0.580798),
    vec2(-0.651784, 0.717887),  vec2(0.222999, -0.215125),
    vec2(0.421003, 0.027070),   vec2(-0.467574, -0.405438),
    vec2(-0.817194, -0.271096), vec2(-0.248268, -0.814753),
    vec2(-0.705374, -0.668203), vec2(0.354411, -0.887570),
    vec2(0.977050, -0.108615),  vec2(0.175817, 0.382366),
    vec2(0.063326, 0.142369),   vec2(0.487472, -0.063082),
    vec2(0.203528, 0.214331),   vec2(-0.084078, 0.898312),
    vec2(-0.667531, 0.326090),  vec2(0.488876, -0.783441),
    vec2(-0.098422, -0.295755), vec2(0.470016, 0.217933),
    vec2(-0.885922, 0.215369),  vec2(-0.696890, -0.549791),
    vec2(0.566637, 0.605213),   vec2(-0.149693, 0.605762),
    vec2(0.039766, -0.396100),  vec2(0.034211, 0.979980)
);

vec2 ReprojectShadow(in vec3 world_pos)
{
	vec3 WorldPos = world_pos;

	vec4 ProjectedPosition = u_ShadowProjection * u_ShadowView * vec4(WorldPos, 1.0f);
	ProjectedPosition.xyz /= ProjectedPosition.w;
	ProjectedPosition.xy = ProjectedPosition.xy * 0.5f + 0.5f;

	return ProjectedPosition.xy;
}

float ComputeShadow(in vec2 txc)
{
    float shadow;

#ifdef SMOOTH_SHADOW_SAMPLING
    vec2 TexSize = textureSize(u_ShadowTexture, 0);
    vec2 TexelSize = 1.0 / TexSize; 

	for(int x = 0; x <= PCF_COUNT; x++)
	{
        float noise = texture(u_BlueNoiseTextures, vec3(gl_FragCoord.xy / textureSize(u_BlueNoiseTextures, 0).xy, 0.0f)).r;
        float theta = noise * 6.28318530718;
        float cosTheta = cos(theta);
        float sinTheta = sin(theta);
        mat2 dither = mat2(vec2(cosTheta, -sinTheta), vec2(sinTheta, cosTheta));

		vec2 jitter_value;
        jitter_value = PoissonDisk[x] * dither;

        float pcf = texture(u_ShadowTexture, txc + jitter_value * TexelSize).r; 
		shadow += pcf;        
	}

	shadow /= float(PCF_COUNT);
#else 
    shadow = textureBicubic(u_ShadowTexture, txc).r;
#endif

    return shadow;
}

bool IsInScreenSpaceBounds(in vec2 tx)
{
    if (tx.x > 0.0f && tx.y > 0.0f && tx.x < 1.0f && tx.y < 1.0f)
    {
        return true;
    }

    return false;
}

// COLORS //
const vec3 SUN_COLOR = (vec3(192.0f, 216.0f, 255.0f) / 255.0f) * 6.4f;
const vec3 SUN_AMBIENT = (vec3(120.0f, 172.0f, 255.0f) / 255.0f) * 0.18f;

void main()
{
    vec4 WorldPosition = texture(u_InitialTracePositionTexture, v_TexCoords);
    vec3 SampledNormals = texture(u_NormalTexture, v_TexCoords).rgb;

    o_Color = vec3(1.0f);
    o_Color = texture(u_Skybox, normalize(v_RayDirection)).rgb * 0.5f;

    if (WorldPosition.w > 0.0f)
    {
        vec2 ReprojectedShadowPos = ReprojectShadow(WorldPosition.xyz);
        float RayTracedShadow = 0.0f;
        
        if (IsInScreenSpaceBounds(ReprojectedShadowPos))
        {
            RayTracedShadow = ComputeShadow(ReprojectedShadowPos);
        }

        vec2 TexSize = textureSize(u_InitialTracePositionTexture, 0);
        float PixelDepth1 = texture(u_InitialTracePositionTexture, clamp(v_TexCoords + vec2(0.0f, 1.0f) * (1.0f / TexSize), 0.001f, 0.999f)).w;
        float PixelDepth2 = texture(u_InitialTracePositionTexture, clamp(v_TexCoords + vec2(0.0f, -1.0f) * (1.0f / TexSize), 0.001f, 0.999f)).w;
        float PixelDepth3 = texture(u_InitialTracePositionTexture, clamp(v_TexCoords + vec2(1.0f, 0.0f) * (1.0f / TexSize), 0.001f, 0.999f)).w;
        float PixelDepth4 = texture(u_InitialTracePositionTexture, clamp(v_TexCoords + vec2(-1.0f, 0.0f) * (1.0f / TexSize), 0.001f, 0.999f)).w;

        if (PixelDepth1 > 0.0f && PixelDepth2 > 0.0f && PixelDepth3 > 0.0f && PixelDepth4 > 0.0f)
        {
            vec2 UV;
            vec3 Tangent, Bitangent;

            CalculateVectors(WorldPosition.xyz, SampledNormals, Tangent, Bitangent, UV); UV.y = 1.0f - UV.y;
	        mat3 tbn = mat3(normalize(Tangent), normalize(Bitangent), normalize(SampledNormals));

            vec4 data = texture(u_DataTexture, v_TexCoords);
            vec3 AlbedoColor = texture(u_BlockAlbedoTextures, vec3(UV, data.x)).rgb;
            vec3 NormalMapped = tbn * (texture(u_BlockNormalTextures, vec3(UV, data.y)).rgb * 2.0f - 1.0f);
            vec3 PBRMap = texture(u_BlockPBRTextures, vec3(UV, data.z)).rgb;

            //vec3 Diffuse = BilateralUpsample(u_DiffuseTexture, v_TexCoords, SampledNormals, WorldPosition.z).rgb;
            vec3 Diffuse = textureBicubic(u_DiffuseTexture, v_TexCoords).rgb;

            vec3 LightAmbience = (vec3(120.0f, 172.0f, 255.0f) / 255.0f) * 1.01f;
            vec3 Ambient = (AlbedoColor * LightAmbience) * 0.005;

            o_Color = (AlbedoColor * SUN_AMBIENT) + (sqrt((Diffuse)) * 3.5f) * CalculateDirectionalLight(WorldPosition.xyz, normalize(u_SunDirection), SUN_COLOR, AlbedoColor, NormalMapped, PBRMap, RayTracedShadow);
            return;
        }
    }

    else 
    {
        vec3 ray_dir = normalize(v_RayDirection); bool intersect_body = false;

        if(dot(ray_dir, normalize(u_SunDirection)) > 0.9997f)
        {
            o_Color = vec3(4.0f) * 3.0f; intersect_body = true;
        }

        if(dot(ray_dir, normalize(u_MoonDirection)) > 0.99986f)
        {
            o_Color = vec3(0.6f, 0.6f, 0.9f) * 50.0f; intersect_body = true;
        }
    }


}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.001); 
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 CalculateDirectionalLight(vec3 world_pos, vec3 light_dir, vec3 radiance, vec3 albedo, vec3 normal, vec3 pbr, float shadow)
{
    float ShadowIntensity = 0.98f;
    float Shadow = min(shadow * ShadowIntensity, 1.0f);

	vec3 V = normalize(u_ViewerPosition - world_pos);
    vec3 L = normalize(light_dir);
    vec3 H = normalize(V + L);

    float Roughness = pbr.r;
    float Metalness = pbr.g;

    float NDF = DistributionGGX(normal, H, Roughness);   
    float G = GeometrySmith(normal, V, L, Roughness);      
    vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), vec3(0.04));
       
    vec3 nominator = NDF * G * F; 
    float denominator = 4.0f * max(dot(normal, V), 0.0) * max(dot(normal, L), 0.0);
    vec3 specular = nominator / max(denominator, 0.001f);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - Metalness;	

    float NdotL = max(dot(normal, L), 0.0);
	vec3 Result = (kD * albedo / PI + (specular)) * radiance * NdotL;

    return Result * (1.0f - Shadow);
}

vec4 cubic(float v){
    vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
    vec4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return vec4(x, y, z, w) * (1.0/6.0);
}

vec4 textureBicubic(sampler2D sampler, vec2 texCoords)
{

   vec2 texSize = textureSize(sampler, 0);
   vec2 invTexSize = 1.0 / texSize;

   texCoords = texCoords * texSize - 0.5;


    vec2 fxy = fract(texCoords);
    texCoords -= fxy;

    vec4 xcubic = cubic(fxy.x);
    vec4 ycubic = cubic(fxy.y);

    vec4 c = texCoords.xxyy + vec2 (-0.5, +1.5).xyxy;

    vec4 s = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    vec4 offset = c + vec4 (xcubic.yw, ycubic.yw) / s;

    offset *= invTexSize.xxyy;

    vec4 sample0 = texture(sampler, offset.xz);
    vec4 sample1 = texture(sampler, offset.yz);
    vec4 sample2 = texture(sampler, offset.xw);
    vec4 sample3 = texture(sampler, offset.yw);

    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return mix(
       mix(sample3, sample2, sx), mix(sample1, sample0, sx)
    , sy);
}

void CalculateVectors(vec3 world_pos, in vec3 normal, out vec3 tangent, out vec3 bitangent, out vec2 uv)
{
	// Hard coded normals, tangents and bitangents

    const vec3 Normals[6] = vec3[]( vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, -1.0f),
					vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f), 
					vec3(-1.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f)
			      );

	const vec3 Tangents[6] = vec3[]( vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f),
					 vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f),
					 vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 0.0f, -1.0f)
				   );

	const vec3 BiTangents[6] = vec3[]( vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f),
				     vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, 1.0f),
					 vec3(0.0f, -1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f)
	);

	if (normal == Normals[0])
    {
        uv = vec2(fract(world_pos.xy));
		tangent = Tangents[0];
		bitangent = BiTangents[0];
    }

    else if (normal == Normals[1])
    {
        uv = vec2(fract(world_pos.xy));
		tangent = Tangents[1];
		bitangent = BiTangents[1];
    }

    else if (normal == Normals[2])
    {
        uv = vec2(fract(world_pos.xz));
		tangent = Tangents[2];
		bitangent = BiTangents[2];
    }

    else if (normal == Normals[3])
    {
        uv = vec2(fract(world_pos.xz));
		tangent = Tangents[3];
		bitangent = BiTangents[3];
    }
	
    else if (normal == Normals[4])
    {
        uv = vec2(fract(world_pos.zy));
		tangent = Tangents[4];
		bitangent = BiTangents[4];
    }
    

    else if (normal == Normals[5])
    {
        uv = vec2(fract(world_pos.zy));
		tangent = Tangents[5];
		bitangent = BiTangents[5];
    }
}

