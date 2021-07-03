#version 330 core

#define CLOUD_HEIGHT 70
#define PCF_COUNT 6 // we really dont care about the low sample count because we have taa.
//#define POISSON_DISK_SAMPLING
#define PI 3.14159265359
#define THRESH 1.41414

layout (location = 0) out vec3 o_Color;
layout (location = 1) out vec3 o_Normal;
layout (location = 2) out vec4 o_PBR;

in vec2 v_TexCoords;
in vec3 v_RayOrigin;
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
uniform sampler2DArray u_BlockEmissiveTextures;
uniform samplerCube u_Skybox;
uniform sampler2D u_ReflectionTraceTexture;
uniform sampler2D u_CloudData;
uniform sampler2D u_PreviousNormalTexture; 

uniform vec3 u_SunDirection;
uniform vec3 u_MoonDirection;
uniform vec3 u_StrongerLightDirection;

uniform float u_Time;
uniform float u_GrassblockAlbedoID;

uniform mat4 u_ShadowView;
uniform mat4 u_ShadowProjection;
uniform mat4 u_ReflectionView;
uniform mat4 u_ReflectionProjection;

uniform vec3 u_ViewerPosition;
uniform vec2 u_Dimensions;

uniform bool u_CloudsEnabled;
uniform bool u_POM = false;
uniform bool u_HighQualityPOM = false;
uniform bool u_RTAO;

uniform float u_CloudBoxSize;

vec4 textureBicubic(sampler2D sampler, vec2 texCoords);
vec3 CalculateDirectionalLight(vec3 world_pos, vec3 light_dir, vec3 radiance, vec3 radiance_s, vec3 albedo, vec3 normal, vec3 pbr, float shadow);
void CalculateVectors(vec3 world_pos, in vec3 normal, out vec3 tangent, out vec3 bitangent, out vec2 uv);


const vec3 ATMOSPHERE_SUN_COLOR = vec3(1.0f, 1.0f, 0.5f);
const vec3 ATMOSPHERE_MOON_COLOR =  vec3(0.1f, 0.1f, 1.0f);


vec3 PoissonDisk3D[16] = vec3[](
    vec3(0.488937, 0.374798, 0.0314035),
    vec3(0.112522, 0.0911893, 0.932066),
    vec3(0.345347, 0.857173, 0.622028),
    vec3(0.724845, 0.0422376, 0.754479),
    vec3(0.775262, 0.82693, 0.16596),
    vec3(0.971221, 0.020539, 0.0113529),
    vec3(0.010834, 0.171209, 0.379254),
    vec3(0.577593, 0.514908, 0.977874),
    vec3(0.170507, 0.840266, 0.0510269),
    vec3(0.939055, 0.566179, 0.568987),
    vec3(0.015137, 0.606647, 0.998566),
    vec3(0.687002, 0.465712, 0.479293),
    vec3(0.170232, 0.25837, 0.602069),
    vec3(0.83755 , 0.334819, 0.0497452),
    vec3(0.795679, 0.742149, 0.878201),
    vec3(0.180761, 0.585253, 0.245888)
);


float Noise2d( in vec2 x )
{
    float xhash = cos( x.x * 37.0 );
    float yhash = cos( x.y * 57.0 );
    return fract( 415.92653 * ( xhash + yhash ) );
}

float NoisyStarField( in vec2 vSamplePos, float fThreshhold )
{
    float StarVal = Noise2d( vSamplePos );
    if ( StarVal >= fThreshhold )
        StarVal = pow( (StarVal - fThreshhold)/(1.0 - fThreshhold), 6.0 );
    else
        StarVal = 0.0;
    return StarVal;
}

// Original star shader by : https://www.shadertoy.com/view/Md2SR3
float StableStarField( in vec2 vSamplePos, float fThreshhold )
{
    float fractX = fract( vSamplePos.x );
    float fractY = fract( vSamplePos.y );
    vec2 floorSample = floor( vSamplePos );
    float v1 = NoisyStarField( floorSample, fThreshhold );
    float v2 = NoisyStarField( floorSample + vec2( 0.0, 1.0 ), fThreshhold );
    float v3 = NoisyStarField( floorSample + vec2( 1.0, 0.0 ), fThreshhold );
    float v4 = NoisyStarField( floorSample + vec2( 1.0, 1.0 ), fThreshhold );

    float StarVal =   v1 * ( 1.0 - fractX ) * ( 1.0 - fractY )
        			+ v2 * ( 1.0 - fractX ) * fractY
        			+ v3 * fractX * ( 1.0 - fractY )
        			+ v4 * fractX * fractY;
	return StarVal;
}

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

float stars(vec3 fragpos)
{
    fragpos.y = abs(fragpos.y);
	float elevation = clamp(fragpos.y, 0.0f, 1.0f);
	vec2 uv = fragpos.xz / (1.0f + elevation);

    float star = StableStarField(uv * 700.0f, 0.999);
    star *= 2.0f;
    float rand_val = rand(fragpos.xy);
	return clamp(star, 0.0f, 100000.0f) * 30.0f;
}

vec2 RayBoxIntersect(vec3 boundsMin, vec3 boundsMax, vec3 rayOrigin, vec3 invRaydir)
{
	vec3 t0 = (boundsMin - rayOrigin) * invRaydir;
	vec3 t1 = (boundsMax - rayOrigin) * invRaydir;
	vec3 tmin = min(t0, t1);
	vec3 tmax = max(t0, t1);
	
	float dstA = max(max(tmin.x, tmin.y), tmin.z);
	float dstB = min(tmax.x, min(tmax.y, tmax.z));
	
	// CASE 1: ray intersects box from outside (0 <= dstA <= dstB)
	// dstA is dst to nearest intersection, dstB dst to far intersection
	
	// CASE 2: ray intersects box from inside (dstA < 0 < dstB) 
	// dstA is the dst to intersection behind the ray, dstB is dst to forward intersection
	
	// CASE 3: ray misses box (dstA > dstB)
	
	float dstToBox = max(0, dstA);
	float dstInsideBox = max(0, dstB - dstToBox);
	return vec2(dstToBox, dstInsideBox);
}

bool GetAtmosphere(inout vec3 atmosphere_color, in vec3 in_ray_dir)
{
    vec3 sun_dir = normalize(u_SunDirection); 
    vec3 moon_dir = vec3(-sun_dir.x, -sun_dir.y, sun_dir.z); 

    vec3 ray_dir = normalize(in_ray_dir);
    
    if(dot(ray_dir, sun_dir) > 0.999825f)
    {
        atmosphere_color = ATMOSPHERE_SUN_COLOR; return true;
    }

    if(dot(ray_dir, moon_dir) > 0.99986f)
    {
        atmosphere_color = ATMOSPHERE_MOON_COLOR; return true;
    }

    vec3 atmosphere = texture(u_Skybox, ray_dir).rgb;

    float star_visibility;
    star_visibility = clamp(dot(u_SunDirection, vec3(0.0f, 1.0f, 0.0f)) + 0.05f, 0.0f, 0.1f) * 12.0; 
    star_visibility = 1.0f - star_visibility;
    vec3 stars = vec3(stars(vec3(in_ray_dir)) * star_visibility);
    stars = clamp(stars, 0.0f, 1.3f);

    atmosphere += stars;

    atmosphere_color = atmosphere;

    return false;
}

vec3 GetAtmosphereAndClouds(vec3 Sky)
{
    if (!u_CloudsEnabled)
    {
        return Sky;
    }

    float BoxSize = (u_CloudBoxSize - 8.0f); // -8 to reduce artifacts.
    vec3 origin = vec3(v_RayOrigin.x, 0.0f, v_RayOrigin.z);
    vec2 Dist = RayBoxIntersect(origin + vec3(-BoxSize, CLOUD_HEIGHT, -BoxSize), origin + vec3(BoxSize, CLOUD_HEIGHT - 12, BoxSize), v_RayOrigin, 1.0f / (v_RayDirection));
    bool Intersect = !(Dist.y == 0.0f);

    if (!Intersect)
    {
        return Sky;
    }

    float SunVisibility = clamp(dot(u_SunDirection, vec3(0.0f, 1.0f, 0.0f)) + 0.05f, 0.0f, 0.1f) * 12.0; SunVisibility = 1.0f  - SunVisibility;
    const vec3 D = (vec3(355.0f, 10.0f, 0.0f) / 255.0f) * 0.4f;
    vec3 S = vec3(1.45f);
    float DuskVisibility = clamp(pow(distance(u_SunDirection.y, 1.0), 1.8f), 0.0f, 1.0f);
    S = mix(S, D, DuskVisibility);
    vec3 M = mix(S + 0.001f, (vec3(46.0f, 142.0f, 255.0f) / 255.0f) * 0.1f, SunVisibility); 
	vec3 IntersectionPosition = v_RayOrigin + (normalize(v_RayDirection) * Dist.x);
	vec4 SampledCloudData = texture(u_CloudData, v_TexCoords).rgba;
    vec3 Scatter = SampledCloudData.xyz;
    float Transmittance = SampledCloudData.w;
    return (Sky * 1.0f) * clamp(Transmittance, 0.95f, 1.0f) + (Scatter * 1.0f * M); // see ya pbr
}

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

vec2 ReprojectShadow(in vec3 world_pos)
{
	vec3 WorldPos = world_pos;

	vec4 ProjectedPosition = u_ShadowProjection * u_ShadowView * vec4(WorldPos, 1.0f);
	ProjectedPosition.xyz /= ProjectedPosition.w;
	ProjectedPosition.xy = ProjectedPosition.xy * 0.5f + 0.5f;

	return ProjectedPosition.xy;
}

vec2 ReprojectReflection(in vec3 world_pos)
{
	vec3 WorldPos = world_pos;

	vec4 ProjectedPosition = u_ReflectionProjection * u_ReflectionView * vec4(WorldPos, 1.0f);
	ProjectedPosition.xyz /= ProjectedPosition.w;
	ProjectedPosition.xy = ProjectedPosition.xy * 0.5f + 0.5f;

	return ProjectedPosition.xy;
}

int MIN = -2147483648;
int MAX = 2147483647;

int xorshift(in int value) 
{
    // Xorshift*32
    // Based on George Marsaglia's work: http://www.jstatsoft.org/v08/i14/paper
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

int nextInt(inout int seed) 
{
    seed = xorshift(seed);
    return seed;
}

float nextFloat(inout int seed) 
{
    seed = xorshift(seed);
    // FIXME: This should have been a seed mapped from MIN..MAX to 0..1 instead
    return abs(fract(float(seed) / 3141.592653));
}

float nextFloat(inout int seed, in float max) 
{
    return nextFloat(seed) * max;
}

float nextFloat(inout int seed, in float min, in float max) 
{
    return min + (max - min) * nextFloat(seed);
}


bool RayBoxIntersect(const vec3 boxMin, const vec3 boxMax, vec3 r0, vec3 rD, out float t_min, out float t_max) 
{
	vec3 inv_dir = 1.0f / rD;
	vec3 tbot = inv_dir * (boxMin - r0);
	vec3 ttop = inv_dir * (boxMax - r0);
	vec3 tmin = min(ttop, tbot);
	vec3 tmax = max(ttop, tbot);
	vec2 t = max(tmin.xx, tmin.yz);
	float t0 = max(t.x, t.y);
	t = min(tmax.xx, tmax.yz);
	float t1 = min(t.x, t.y);
	t_min = t0;
	t_max = t1;
	return t1 > max(t0, 0.0);
}

int RNG_SEED;

float ComputeShadow(vec3 world_pos)
{
    float shadow = 0.0;

    vec2 TexSize = textureSize(u_ShadowTexture, 0);
    vec2 TexelSize = 1.0 / TexSize; 
    int AVG = 0;
    float Noise = nextFloat(RNG_SEED);

    vec3 ShadowDirection = normalize(u_StrongerLightDirection);
  
	for(int x = 0; x <= PCF_COUNT; x++)
	{
    #ifdef POISSON_DISK_SAMPLING
        BlueNoise *= (2.0f * PI);
        float SinTheta = sin(Noise);
        float CosTheta = cos(Noise);
        
        mat3 RotationMatrix = mat3(vec3(CosTheta, -SinTheta, 0.0f), 
                                   vec3(SinTheta, CosTheta, 0.0f), 
                                   vec3(0.0f, 0.0f, 1.0f));
        
        vec3 RotatedPoissonSample = RotationMatrix * PoissonDisk3D[x];
        
        vec3 SampleWorldPosition = world_pos + (RotatedPoissonSample * 0.05f); 
    #else
        vec3 WhiteNoise = vec3(nextFloat(RNG_SEED), nextFloat(RNG_SEED), nextFloat(RNG_SEED));
        vec3 SampleWorldPosition = world_pos + WhiteNoise * 0.02f;
    #endif

        float ShadowTMIN, ShadowTMAX;
        bool PlayerIntersect = RayBoxIntersect(u_ViewerPosition + vec3(0.2f, 0.0f, 0.2f), u_ViewerPosition - vec3(0.75f, 1.75f, 0.75f), SampleWorldPosition, ShadowDirection, ShadowTMIN, ShadowTMAX);

        if (PlayerIntersect)
        {
            shadow += 1.0f;
            AVG++;
        }

        else
        {
            vec2 ReprojectShadow = ReprojectShadow(SampleWorldPosition);

            if (ReprojectShadow.x > 0.0f && ReprojectShadow.x < 1.0f && ReprojectShadow.y > 0.0f && ReprojectShadow.y < 1.0f)
            {
                float ShadowAt = texture(u_ShadowTexture, ReprojectShadow).r;
		        shadow += ShadowAt;        
                AVG++;
            }
        }
	}

	shadow /= float(AVG);

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

float GetDisplacementAt(in vec2 txc, in float pbridx) 
{
    return texture(u_BlockPBRTextures, vec3(vec2(txc.x, txc.y), pbridx)).b * 0.35f;
}

vec2 ParallaxOcclusionMapping(vec2 TextureCoords, vec3 ViewDirection, in float pbridx) // View direction should be in tangent space!
{ 
    float NumLayers = u_HighQualityPOM ? 72 : 32; 
    float LayerDepth = 1.0 / (u_HighQualityPOM ? (NumLayers * 0.725f) : (NumLayers * 0.65));
    float CurrentLayerDepth = 0.0;
    vec2 P = ViewDirection.xy * 1.0f; 
    vec2 DeltaTexCoords = P / NumLayers;
    vec2 InitialDeltaCoords = DeltaTexCoords;

    vec2  CurrentTexCoords = TextureCoords;
    float CurrentDepthMapValue = GetDisplacementAt(CurrentTexCoords, pbridx);

    for (int i = 0 ; i < NumLayers ; i++)
    {
        if(CurrentLayerDepth < CurrentDepthMapValue)
        {
            //CurrentTexCoords -= max(DeltaTexCoords, InitialDeltaCoords * 0.025f);
            CurrentTexCoords -= DeltaTexCoords;
            CurrentDepthMapValue = GetDisplacementAt(CurrentTexCoords, pbridx);  
            CurrentLayerDepth += LayerDepth;
            DeltaTexCoords *= u_HighQualityPOM ? 0.970f : 0.9f;
        }
    }

    vec2 PrevTexCoords = CurrentTexCoords + DeltaTexCoords;
    float AfterDepth  = CurrentDepthMapValue - CurrentLayerDepth;
    float BeforeDepth = GetDisplacementAt(PrevTexCoords, pbridx) - CurrentLayerDepth + LayerDepth;
    float Weight = AfterDepth / (AfterDepth - BeforeDepth);
    vec2 FinalTexCoords = PrevTexCoords * Weight + CurrentTexCoords * (1.0 - Weight);
    return FinalTexCoords;
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

vec4 DepthOnlyBilateralUpsample(sampler2D tex, vec2 txc, float base_depth)
{
    const vec2 Kernel[4] = vec2[](
        vec2(0.0f, 1.0f),
        vec2(1.0f, 0.0f),
        vec2(-1.0f, 0.0f),
        vec2(0.0, -1.0f)
    );

    vec2 texel_size = 1.0f / textureSize(tex, 0);

    vec4 color = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    float weight_sum;

    for (int i = 0; i < 4; i++) 
    {
		vec4 sampled_pos = texture(u_InitialTracePositionTexture, txc + Kernel[i] * texel_size);

		if (sampled_pos.w <= 0.0f)
		{
			continue;
		}

        float sampled_depth = (sampled_pos.z); 
        float dweight = 1.0f / (abs(base_depth - sampled_depth) + 0.001f);

        float computed_weight = dweight;
        color.rgba += texture(tex, txc + Kernel[i] * texel_size) * computed_weight;
        weight_sum += computed_weight;
    }

    
    color /= weight_sum + 0.01f;
    //color = clamp(color, texture(tex, txc) * 0.12f, vec4(1.0f));
    return color;
}


bool SampleValid(in vec2 SampleCoord, in vec3 InputPosition, in vec3 InputNormal)
{
	bool InScreenSpace = SampleCoord.x > 0.0f && SampleCoord.x < 1.0f && SampleCoord.y > 0.0f && SampleCoord.y < 1.0f;
	vec4 PositionAt = texture(u_InitialTracePositionTexture, SampleCoord);
	vec3 NormalAt = texture(u_NormalTexture, SampleCoord).xyz;
	return (abs(PositionAt.z - InputPosition.z) <= THRESH) 
			&& (abs(PositionAt.x - InputPosition.x) <= THRESH) 
			&& (abs(PositionAt.y - InputPosition.y) <= THRESH) 
			&& (InputNormal == NormalAt) 
			&& (PositionAt.w > 0.0f) && (InScreenSpace);
}

vec4 BilateralUpsample2(sampler2D tex, vec2 txc, vec3 base_pos, vec3 base_normal)
{
    const vec2 Kernel[8] = vec2[8]
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

    const float Weights[8] = float[8](
        1.0f / 16.0f,
        3.0f / 32.0f,
        1.0f / 16.0f,
        3.0f / 32.0f,
        3.0f / 32.0f,
        1.0f / 16.0f,
        3.0f / 32.0f,
        1.0f / 16.0f
    );

    vec2 TexelSize = 1.0f / textureSize(tex, 0);

    vec4 Color = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    float TotalWeights = 0.0f;

    for (int i = 0; i < 8; i++) 
    {
        float weight = Weights[i];

        if (SampleValid(txc + Kernel[i] * TexelSize, base_pos, base_normal))
        {
            Color.rgba += texture(tex, txc + Kernel[i] * TexelSize) * weight;
            TotalWeights += weight;
        }
    }
    
    return Color / max(TotalWeights, 0.01f);
}

vec3 fresnelroughness(vec3 Eye, vec3 norm, vec3 F0, float roughness) 
{
	return F0 + (max(vec3(pow(1.0f - roughness, 3.0f)) - F0, vec3(0.0f))) * pow(max(1.0 - clamp(dot(Eye, norm), 0.0f, 1.0f), 0.0f), 5.0f);
}

bool IsAtEdge(in vec2 txc)
{
    vec2 TexelSize = 1.0f / textureSize(u_InitialTracePositionTexture, 0);

    const vec2 Kernel[8] = vec2[8]
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

    for (int i = 0 ; i < 8 ; i++)
    {
        if (texture(u_InitialTracePositionTexture, txc + Kernel[i] * TexelSize).w <= 0.0f)
        {
            return true;
        }
    }

    return false;
}

// COLORS //
const vec3 SUN_COLOR = (vec3(192.0f, 216.0f, 255.0f) / 255.0f) * 4.0f;
const vec3 NIGHT_COLOR  = (vec3(96.0f, 192.0f, 255.0f) / 255.0f) * 0.25f; 
const vec3 DUSK_COLOR = (vec3(255.0f, 204.0f, 144.0f) / 255.0f) * 0.064f; 

void main()
{
	RNG_SEED = int(gl_FragCoord.x) + int(gl_FragCoord.y) * int(800 * u_Time);

    // Xorshift!
	RNG_SEED ^= RNG_SEED << 13;
    RNG_SEED ^= RNG_SEED >> 17;
    RNG_SEED ^= RNG_SEED << 5;

    vec4 WorldPosition = texture(u_InitialTracePositionTexture, v_TexCoords);

    vec3 SampledNormals = texture(u_NormalTexture, v_TexCoords).rgb;
    vec3 AtmosphereAt = vec3(0.0f);

    o_Color = vec3(1.0f);
    bool BodyIntersect = GetAtmosphere(AtmosphereAt, v_RayDirection);
    o_PBR.w = float(BodyIntersect);

    if (WorldPosition.w > 0.0f)
    {
        float RayTracedShadow = 0.0f;
        
        RayTracedShadow = ComputeShadow(WorldPosition.xyz);

        if (!IsAtEdge(v_TexCoords))
        {
            vec2 UV;
            vec3 Tangent, Bitangent;

            CalculateVectors(WorldPosition.xyz, SampledNormals, Tangent, Bitangent, UV); 
	        mat3 tbn = mat3(normalize(Tangent), normalize(Bitangent), normalize(SampledNormals));
            vec4 data = texture(u_DataTexture, v_TexCoords);

            if (u_POM && data.r != u_GrassblockAlbedoID)
            {
                vec2 InitialUV = UV;

                if (SampledNormals == vec3(-1.0f, 0.0f, 0.0f)) {  UV.x = 1.0f - UV.x; UV.y = 1.0f - UV.y; }
                if (SampledNormals == vec3(1.0f, 0.0f, 0.0f)) {  UV.y = 1.0f - UV.y; }
                if (SampledNormals == vec3(0.0f, -1.0f, 0.0f)) {  UV.y = 1.0f - UV.y; }
                //if (SampledNormals == vec3(0.0f, 0.0f, 1.0f)) {  flip = true;}
                //if (SampledNormals == vec3(0.0f, 0.0f, -1.0f)) {  flip = true; }
                
                vec3 TangentViewPosition = tbn * u_ViewerPosition;
                vec3 TangentFragPosition = tbn * WorldPosition.xyz; 
                vec3 TangentViewDirection = normalize(TangentFragPosition - TangentViewPosition);
                
                UV = ParallaxOcclusionMapping(UV, TangentViewDirection, data.z);
                //UV.y = flip && data.r == u_GrassblockAlbedoID ? 1.0f - UV.y : UV.y;
            
            } else { UV.y = 1.0f - UV.y; }

            vec4 PBRMap = texture(u_BlockPBRTextures, vec3(UV, data.z)).rgba;
            vec3 AlbedoColor = texture(u_BlockAlbedoTextures, vec3(UV.xy, data.x)).rgb;
            vec3 NormalMapped = tbn * (texture(u_BlockNormalTextures, vec3(UV, data.y)).rgb * 2.0f - 1.0f);
            float Emissivity = data.w > -0.5f ? texture(u_BlockEmissiveTextures, vec3(UV, data.w)).r : 0.0f;

            //vec4 Diffuse = BilateralUpsample(u_DiffuseTexture, v_TexCoords, SampledNormals.xyz, WorldPosition.z);
            //vec4 Diffuse = PositionOnlyBilateralUpsample(u_DiffuseTexture, v_TexCoords, WorldPosition.xyz);
            //vec3 Diffuse = DepthOnlyBilateralUpsample(u_DiffuseTexture, v_TexCoords, WorldPosition.z).xyz;
            vec4 Diffuse = BilateralUpsample2(u_DiffuseTexture, v_TexCoords, WorldPosition.xyz, SampledNormals.xyz).xyzw;
            float AO = texture(u_DiffuseTexture, v_TexCoords).w;

            vec3 LightAmbience = (vec3(120.0f, 172.0f, 255.0f) / 255.0f) * 1.01f;
            vec3 Ambient = (AlbedoColor * LightAmbience) * 0.09f;
            float SampledAO = pow(PBRMap.w, 1.25f);

            float SunVisibility = clamp(dot(u_SunDirection, vec3(0.0f, 1.0f, 0.0f)) + 0.05f, 0.0f, 0.1f) * 12.0; SunVisibility = 1.0f  - SunVisibility;
            float DuskVisibility = clamp(pow(distance(u_SunDirection.y, 1.0), 1.5f), 0.0f, 1.0f);
            vec3 SunColor = mix(SUN_COLOR, DUSK_COLOR * 0.5f, DuskVisibility);
            //vec3 SunColor = SUN_COLOR;
            vec3 SunDirectLighting = CalculateDirectionalLight(WorldPosition.xyz, normalize(u_SunDirection), SunColor, SunColor * 0.4f, AlbedoColor, NormalMapped, PBRMap.xyz, RayTracedShadow);
            vec3 MoonDirectLighting = CalculateDirectionalLight(WorldPosition.xyz, normalize(u_MoonDirection), NIGHT_COLOR, NIGHT_COLOR, AlbedoColor, NormalMapped, PBRMap.xyz, RayTracedShadow);
            vec3 DirectLighting = mix(SunDirectLighting, MoonDirectLighting, SunVisibility * vec3(1.0f));
            
            DirectLighting = (float(!(Emissivity > 0.5f)) * DirectLighting);
            float Roughness = PBRMap.r;
            vec3 SpecularIndirect = texture(u_ReflectionTraceTexture, v_TexCoords).rgb;
            vec3 DiffuseIndirect = (Diffuse.xyz * AlbedoColor);
            vec3 Lo = normalize(u_ViewerPosition - WorldPosition.xyz);
            vec3 F0 = mix(vec3(0.04), AlbedoColor, PBRMap.g);

            vec3 SpecularFactor = fresnelroughness(Lo, NormalMapped.xyz, vec3(F0), Roughness); 
            o_Color = (DirectLighting + ((1.0f - SpecularFactor) * DiffuseIndirect) + 
                      (SpecularFactor * SpecularIndirect * min((PBRMap.g + 1.0f), 1.3f))) 
                      * clamp(SampledAO, 0.2f, 1.01f);

            o_Normal = vec3(NormalMapped.x, NormalMapped.y, NormalMapped.z);
            o_PBR.xyz = PBRMap.xyz;
            o_PBR.w = Emissivity;

            if (!u_RTAO && (distance(WorldPosition.xyz, u_ViewerPosition) < 80)) // -> Causes artifacts if the AO is applied too far away
            {
                float bias = 0.02f;
                if (v_TexCoords.x > bias && v_TexCoords.x < 1.0f - bias &&
                    v_TexCoords.y > bias && v_TexCoords.y < 1.0f - bias)
                {
                    o_Color *= vec3(clamp(pow(AO, 1.0f), 0.0f, 1.0f));
                }
            }

            return;
        }

        else 
        {   
            vec3 CloudAndSky = GetAtmosphereAndClouds(AtmosphereAt);
            o_Color = (CloudAndSky);
            o_Normal = vec3(-1.0f);
            o_PBR.xyz = vec3(-1.0f);
            o_PBR.w = float(BodyIntersect);
        }
    }

    else 
    {   
        vec3 CloudAndSky = GetAtmosphereAndClouds(AtmosphereAt);
        o_Color = (CloudAndSky);
        o_Normal = vec3(-1.0f);
        o_PBR.xyz = vec3(-1.0f);
        o_PBR.w = float(BodyIntersect);
    }
}

float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 CalculateDirectionalLight(vec3 world_pos, vec3 light_dir, vec3 radiance, vec3 radiance_s, vec3 albedo, vec3 normal, vec3 pbr, float shadow)
{
    const float Epsilon = 0.00001;
    float Shadow = min(shadow, 1.0f);

    vec3 Lo = normalize(u_ViewerPosition - world_pos);

	vec3 N = normal;
	float cosLo = max(0.0, dot(N, Lo));
	vec3 Lr = 2.0 * cosLo * N - Lo;
	vec3 F0 = mix(vec3(0.04), albedo, pbr.g);

    vec3 Li = light_dir;
	vec3 Lradiance = radiance;

	vec3 Lh = normalize(Li + Lo);

	float cosLi = max(0.0, dot(N, Li));
	float cosLh = max(0.0, dot(N, Lh));

	vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
	float D = ndfGGX(cosLh, pbr.r);
	float G = gaSchlickGGX(cosLi, cosLo, pbr.r);

	vec3 kd = mix(vec3(1.0) - F, vec3(0.0), pbr.g);
	vec3 diffuseBRDF = kd * albedo;

	vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo); specularBRDF = clamp(specularBRDF, 0.0f, 2.0f);
	vec3 Result = (diffuseBRDF * Lradiance * cosLi) + (specularBRDF * radiance_s * cosLi);
    return clamp(Result, 0.0f, 2.5) * clamp((1.0f - Shadow), 0.0f, 1.0f);
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