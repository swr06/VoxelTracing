#version 330 core

#define WORLD_SIZE_X 384
#define WORLD_SIZE_Y 128
#define WORLD_SIZE_Z 384
#define PI 3.14159265359

//#define ALBEDO_TEX_LOD 3 // 512, 256, 128
//#define JITTER_BASED_ON_ROUGHNESS

layout (location = 0) out vec3 o_Color;

in vec2 v_TexCoords;

uniform sampler2D u_PositionTexture;
uniform sampler2D u_InitialTraceNormalTexture;
uniform sampler2D u_PBRTexture;
uniform sampler2D u_DataTexture;

//uniform sampler2D u_BlueNoiseTexture;

uniform sampler3D u_VoxelData;
uniform sampler3D u_DistanceFieldTexture;

uniform bool u_RoughReflections;

uniform samplerCube u_Skymap;

uniform vec4 BLOCK_TEXTURE_DATA[128];

uniform float u_ReflectionTraceRes;

uniform vec2 u_Dimensions;
uniform vec3 u_SunDirection;
uniform vec3 u_StrongerLightDirection;
uniform float u_Time;

uniform int u_GrassBlockProps[10];

uniform sampler2DArray u_BlockNormalTextures;
uniform sampler2DArray u_BlockAlbedoTextures;
uniform sampler2DArray u_BlockPBRTextures;
uniform sampler2DArray u_BlockEmissiveTextures;

uniform vec3 u_ViewerPosition;
uniform int u_SPP;

uniform int u_CurrentFrame;


		
// Function prototypes
void CalculateUV(vec3 world_pos, in vec3 normal, out vec2 uv);
void CalculateVectors(vec3 world_pos, in vec3 normal, out vec3 tangent, out vec3 bitangent, out vec2 uv);
float VoxelTraversalDF(vec3 origin, vec3 direction, inout vec3 normal, inout float blockType, bool shadow);
float GetVoxel(ivec3 loc);
float GetShadowAt(in vec3 pos, in vec3 ldir);

int MIN = -2147483648;
int MAX = 2147483647;
int RNG_SEED;

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

bool PointIsInSphere(vec3 point, float radius)
{
	return ((point.x * point.x) + (point.y * point.y) + (point.z * point.z)) < (radius * radius);
}

vec3 RandomPointInUnitSphereRejective()
{
	float x, y, z;
	const int accuracy = 10;

	for (int i = 0 ; i < clamp(accuracy, 2, 40); i++)
	{
		x = nextFloat(RNG_SEED, -1.0f, 1.0f);
		y = nextFloat(RNG_SEED, -1.0f, 1.0f);
		z = nextFloat(RNG_SEED, -1.0f, 1.0f);

		if (PointIsInSphere(vec3(x, y, z), 1.0f))
		{
			return vec3(x, y, z);
		}
	}

	return vec3(x, y, z);
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

vec3 CalculateDirectionalLight(vec3 world_pos, vec3 light_dir, vec3 radiance, vec3 albedo, vec3 normal, vec3 pbr, float shadow)
{
    const float Epsilon = 0.00001;
    float Shadow = min(shadow, 1.0f);

    vec3 Lo = normalize(u_ViewerPosition - world_pos);

	vec3 N = normal;
	float cosLo = max(0.0, dot(N, Lo));
	vec3 Lr = 2.0 * cosLo * N - Lo;
	vec3 F0 = mix(vec3(0.04), albedo, pbr.g);
    vec3 Li = light_dir; // no need to normalize
	vec3 Lradiance = radiance;
	vec3 Lh = normalize(Li + Lo);
	float cosLi = max(0.0, dot(N, Li));
	float cosLh = max(0.0, dot(N, Lh));
	vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
	float D = ndfGGX(cosLh, pbr.r);
	float G = gaSchlickGGX(cosLi, cosLo, pbr.r);
	vec3 kd = mix(vec3(1.0) - F, vec3(0.0), pbr.g);
	vec3 diffuseBRDF = kd * albedo;
	vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);
	specularBRDF *= 0.2f; // Specular highlight isnt too important
	vec3 Result = (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
    return max(Result, 0.0f) * clamp((1.0f - Shadow), 0.0f, 1.0f);
}

vec3 ImportanceSampleGGX(vec3 N, float roughness, vec2 Xi)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
	
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
	
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
} 

float RadicalInverse_VdC(uint bits) 
{
     bits = (bits << 16u) | (bits >> 16u);
     bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
     bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
     bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
     bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
     return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

const vec3 ATMOSPHERE_SUN_COLOR = vec3(1.0f * 6.25f, 1.0f * 6.25f, 0.8f * 4.0f);
const vec3 ATMOSPHERE_MOON_COLOR =  vec3(0.7f, 0.7f, 1.25f);

void GetAtmosphere(inout vec3 atmosphere_color, in vec3 in_ray_dir)
{
    vec3 sun_dir = normalize(u_SunDirection); 
    vec3 moon_dir = vec3(-sun_dir.x, -sun_dir.y, sun_dir.z); 

    vec3 ray_dir = normalize(in_ray_dir);
    vec3 atmosphere = texture(u_Skymap, ray_dir).rgb;

    atmosphere_color = atmosphere;
}

const vec3 SUN_COLOR = (vec3(192.0f, 216.0f, 255.0f) / 255.0f) * 3.0f;
const vec3 SUN_AMBIENT = (vec3(120.0f, 172.0f, 255.0f) / 255.0f) * 4.2f;

const vec3 NIGHT_COLOR  = (vec3(96.0f, 192.0f, 255.0f) / 255.0f) * 0.3f; 
const vec3 NIGHT_AMBIENT  = (vec3(96.0f, 192.0f, 255.0f) / 255.0f) * 0.76f; 

const vec3 NORMAL_TOP = vec3(0.0f, 1.0f, 0.0f);
const vec3 NORMAL_BOTTOM = vec3(0.0f, -1.0f, 0.0f);
const vec3 NORMAL_FRONT = vec3(0.0f, 0.0f, 1.0f);
const vec3 NORMAL_BACK = vec3(0.0f, 0.0f, -1.0f);
const vec3 NORMAL_LEFT = vec3(-1.0f, 0.0f, 0.0f);
const vec3 NORMAL_RIGHT = vec3(1.0f, 0.0f, 0.0f);

bool GetPlayerIntersect(in vec3 pos, in vec3 ldir);

void main()
{
	RNG_SEED = int(gl_FragCoord.x) + int(gl_FragCoord.y) * 800 * int(floor(u_Time * 100));
	RNG_SEED ^= RNG_SEED << 13;
    RNG_SEED ^= RNG_SEED >> 17;
    RNG_SEED ^= RNG_SEED << 5;

	vec2 Pixel;
	Pixel.x = v_TexCoords.x * u_Dimensions.x;
	Pixel.y = v_TexCoords.y * u_Dimensions.y;

	int SPP = clamp(u_SPP, 1, 64);
	int total_hits = 0;
	vec3 TotalColor = vec3(0.0f);

	vec2 suv = v_TexCoords;
	vec4 SampledWorldPosition = texture(u_PositionTexture, suv); // initial intersection point

	if (SampledWorldPosition.w <= 0.0001f)
	{
		o_Color = vec3(0.0f);
		return;
	}

	vec3 InitialTraceNormal = texture(u_InitialTraceNormalTexture, suv).rgb;
	vec4 data = texture(u_PBRTexture, suv);

	vec2 iUV; 
	vec3 iTan, iBitan;
	CalculateVectors(SampledWorldPosition.xyz, InitialTraceNormal, iTan, iBitan, iUV);
	
	vec4 PBRMap = texture(u_BlockPBRTextures, vec3(iUV, data.z)).rgba;
	float RoughnessAt = PBRMap.r;
	float MetalnessAt = PBRMap.g;
	vec3 I = normalize(SampledWorldPosition.xyz - u_ViewerPosition);

	mat3 tbn = mat3(normalize(iTan), normalize(iBitan), normalize(InitialTraceNormal));
	vec3 NormalMappedInitial = tbn*(texture(u_BlockNormalTextures, vec3(iUV, data.g)).rgb * 2.0f - 1.0f);
	SampledWorldPosition.xyz += InitialTraceNormal.xyz * 0.015f; // Apply bias.

	float ComputedShadow = 0.0f;
	int ShadowItr = 0;

	vec3 NormalizedStrongerDir = normalize(u_StrongerLightDirection);

	float HitDistance = -1.0f;

	for (int s = 0 ; s < SPP ; s++)
	{
		//if (MetalnessAt < 0.025f) 
		//{
		//	continue;
		//}

		vec2 Xi;
		//Xi = Hammersley(s, SPP);
		Xi = vec2(nextFloat(RNG_SEED), nextFloat(RNG_SEED)); // We want the samples to converge faster! 
		Xi = Xi * vec2(1.0f, 0.75f); // Reduce the variance and crease clarity
		vec3 ReflectionNormal = u_RoughReflections ? (RoughnessAt > 0.075f ? ImportanceSampleGGX(NormalMappedInitial, RoughnessAt, Xi) : NormalMappedInitial) : NormalMappedInitial;
		
		vec3 R = normalize(reflect(I, ReflectionNormal));
		vec3 Normal;
		float Blocktype;

		float T = VoxelTraversalDF(SampledWorldPosition.xyz, R, Normal, Blocktype, false);
		vec3 HitPosition = SampledWorldPosition.xyz + (R * T);

		vec2 UV; 
		vec3 Tangent, Bitangent;
		CalculateVectors(HitPosition, Normal, Tangent, Bitangent, UV); UV.y = 1.0f - UV.y;

		if (T > 0.0f)
		{
			HitDistance = max(HitDistance, T);
			int reference_id = clamp(int(floor(Blocktype * 255.0f)), 0, 127);
			vec4 texture_ids = BLOCK_TEXTURE_DATA[reference_id];

			// I hate this.
			if (reference_id == u_GrassBlockProps[0])
			{
			    if (Normal == NORMAL_LEFT || Normal == NORMAL_RIGHT || Normal == NORMAL_FRONT || Normal == NORMAL_BACK)
				{
					texture_ids.x = u_GrassBlockProps[4];
					texture_ids.y = u_GrassBlockProps[5];
					texture_ids.z = u_GrassBlockProps[6];
				}

				else if (Normal == NORMAL_TOP)
				{
					texture_ids.x = u_GrassBlockProps[1];
					texture_ids.y = u_GrassBlockProps[2];
					texture_ids.z = u_GrassBlockProps[3];
				}

				else if (Normal == NORMAL_BOTTOM)
				{
					texture_ids.x = u_GrassBlockProps[7];
					texture_ids.y = u_GrassBlockProps[8];
					texture_ids.z = u_GrassBlockProps[9];
				}
			}

			mat3 TBN;
			TBN = mat3(normalize(Tangent), normalize(Bitangent), normalize(Normal));

			vec3 Albedo = textureLod(u_BlockAlbedoTextures, vec3(UV,texture_ids.x), 2).rgb;
			bool SunStronger = u_StrongerLightDirection == u_SunDirection;
			vec3 Radiance = SunStronger ? SUN_COLOR : NIGHT_COLOR * 0.7500f; 
			vec3 Ambient = SunStronger ? SUN_AMBIENT : NIGHT_AMBIENT;
			Ambient = (Albedo * Ambient);
				
			vec4 SampledPBR = textureLod(u_BlockPBRTextures, vec3(UV, texture_ids.z), 3).rgba;
			float AO = pow(SampledPBR.w, 2.0f);

			// Compute shadow rays for only 1/4 the reflection samples because performance :p
			if ((ShadowItr < max(SPP / 4, 1))) 
			{
				ComputedShadow = GetShadowAt(HitPosition + Normal*0.035f, NormalizedStrongerDir);
				ShadowItr = ShadowItr + 1;
			}

			else
			{
				// basically free so why the fuck not
				ComputedShadow += GetPlayerIntersect(HitPosition + Normal*0.035f, NormalizedStrongerDir) ? 1.0f : 0.0f;
				ComputedShadow = clamp(ComputedShadow, 0.0f, 1.0f);
			}

			vec3 NormalMapped = TBN * (textureLod(u_BlockNormalTextures, vec3(UV,texture_ids.y), 3).rgb * 2.0f - 1.0f);
			vec3 DirectLighting = CalculateDirectionalLight(HitPosition, 
								   NormalizedStrongerDir, 
								   Radiance, 
								   Albedo, 
								   NormalMapped, 
								   SampledPBR.xyz,
								   ComputedShadow);
			
			if (texture_ids.w > 0.0f) 
			{
				float Emissivity = texture(u_BlockEmissiveTextures, vec3(UV, texture_ids.w)).r;
				
				if (Emissivity > 0.2f)
				{
					float m = SPP <= 5 ? 12.5f : 7.0f;
					DirectLighting = Albedo * max(Emissivity * m, 2.0f);
				}
			}
			
			vec3 Computed;
			Computed = DirectLighting;
			Computed *= AO;
			TotalColor += Computed;

		}

		else
		{
			vec3 AtmosphereColor;

			vec3 AtmosphereRayDir = R;

			GetAtmosphere(AtmosphereColor, AtmosphereRayDir);
			TotalColor += AtmosphereColor;
		}


		total_hits++;
	}

	o_Color = total_hits > 0 ? (TotalColor / float(total_hits)) : vec3(0.0f);
	o_Color = clamp(o_Color, 0.0f, 1.0f);
}

bool IsInVolume(in vec3 pos)
{
    if (pos.x < 0.0f || pos.y < 0.0f || pos.z < 0.0f || 
        pos.x > float(WORLD_SIZE_X - 1) || pos.y > float(WORLD_SIZE_Y - 1) || pos.z > float(WORLD_SIZE_Z - 1))
    {
        return false;    
    }   

    return true;
}

float GetVoxel(ivec3 loc)
{
    if (IsInVolume(loc))
    {
        return texelFetch(u_VoxelData, loc, 0).r;
    }
    
    return 0.0f;
}

float ToConservativeEuclidean(float Manhattan)
{
	return Manhattan == 1 ? 1 : Manhattan * 0.57735026918f;
}

float GetDistance(ivec3 loc)
{
    if (IsInVolume(loc))
    {
         return (texelFetch(u_DistanceFieldTexture, loc, 0).r);
    }
    
    return -1.0f;
}

bool VoxelExists(in vec3 loc)
{
    if (GetVoxel(ivec3(loc)) > 0.0f) 
    {
        return true;
    }

    return false;
}

float VoxelTraversalDF(vec3 origin, vec3 direction, inout vec3 normal, inout float blockType, bool shadow) 
{
	vec3 initial_origin = origin;
	const float epsilon = 0.01f;
	bool Intersection = false;

	int MinIdx = 0;
	ivec3 RaySign = ivec3(sign(direction));

	int itr = 0;
	int sz = shadow ? 150 : 50;

	for (itr = 0 ; itr < sz ; itr++)
	{
		ivec3 Loc = ivec3(floor(origin));
		
		if (!IsInVolume(Loc))
		{
			Intersection = false;
			break;
		}

		float Dist = GetDistance(Loc) * 255.0f; 
		int Euclidean = int(floor(ToConservativeEuclidean(Dist)));

		if (Euclidean == 0)
		{
			break;
		}

		if (Euclidean == 1)
		{
			// Do the DDA algorithm for one voxel 

			ivec3 GridCoords = ivec3(origin);
			vec3 WithinVoxelCoords = origin - GridCoords;
			vec3 DistanceFactor = (((1 + RaySign) >> 1) - WithinVoxelCoords) * (1.0f / direction);

			MinIdx = DistanceFactor.x < DistanceFactor.y && RaySign.x != 0
				? (DistanceFactor.x < DistanceFactor.z || RaySign.z == 0 ? 0 : 2)
				: (DistanceFactor.y < DistanceFactor.z || RaySign.z == 0 ? 1 : 2);

			GridCoords[MinIdx] += RaySign[MinIdx];
			WithinVoxelCoords += direction * DistanceFactor[MinIdx];
			WithinVoxelCoords[MinIdx] = 1 - ((1 + RaySign) >> 1) [MinIdx]; // Bit shifts (on ints) to avoid division

			origin = GridCoords + WithinVoxelCoords;
			origin[MinIdx] += RaySign[MinIdx] * 0.0001f;

			Intersection = true;
		}

		else 
		{
			origin += int(Euclidean - 1) * direction;
		}
	}

	if (Intersection)
	{
		normal = vec3(0.0f);
		normal[MinIdx] = -RaySign[MinIdx];
		blockType = GetVoxel(ivec3(floor(origin)));
		return blockType > 0.0f ? distance(origin, initial_origin) : -1.0f;
	}

	return -1.0f;
}

bool RayBoxIntersect(const vec3 boxMin, const vec3 boxMax, vec3 r0, vec3 rD, out float t_min, out float t_max);

bool GetPlayerIntersect(in vec3 pos, in vec3 ldir)
{
	float ShadowTMIN = -1.0f, ShadowTMAX = -1.0f;
	return RayBoxIntersect(u_ViewerPosition + vec3(0.2f, 0.0f, 0.2f), u_ViewerPosition - vec3(0.75f, 1.75f, 0.75f), pos.xyz, ldir, ShadowTMIN, ShadowTMAX);
}	

float GetShadowAt(in vec3 pos, in vec3 ldir)
{
	float T = -1.0f;
	 
	vec3 norm;
	float block;

	if (GetPlayerIntersect(pos, ldir)) { return 1.0f; }
	T = VoxelTraversalDF(pos.rgb, ldir, norm, block, true);

	if (T > 0.0f) 
	{ 
		return 1.0f; 
	}
	
	else 
	{ 
		return 0.0f;
	}
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

void CalculateUV(vec3 world_pos, in vec3 normal, out vec2 uv)
{
	// Hard coded normals, tangents and bitangents

    const vec3 Normals[6] = vec3[]( vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, -1.0f),
					vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f), 
					vec3(-1.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f)
			      );

	if (normal == Normals[0])
    {
        uv = vec2(fract(world_pos.xy));
    }

    else if (normal == Normals[1])
    {
        uv = vec2(fract(world_pos.xy));
    }

    else if (normal == Normals[2])
    {
        uv = vec2(fract(world_pos.xz));
    }

    else if (normal == Normals[3])
    {
        uv = vec2(fract(world_pos.xz));
    }
	
    else if (normal == Normals[4])
    {
        uv = vec2(fract(world_pos.zy));
    }
    

    else if (normal == Normals[5])
    {
        uv = vec2(fract(world_pos.zy));
    }
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
