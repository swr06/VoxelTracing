#include "Pipeline.h"
#include <chrono>
#include "ShaderManager.h"
#include "BlockDataSSBO.h"
#include "BlueNoiseDataSSBO.h"
#include "SoundManager.h"
#include "TAAJitter.h"
#include "VolumetricFloodFill.h"

static VoxelRT::Player MainPlayer;
static bool VSync = false;
static bool JitterSceneForTAA = false;

static bool ContrastAdaptiveSharpening = true;
static float CAS_SharpenAmount = 0.25f;

static bool CloudsEnabled = true;
static float CloudCoverage = 0.08650f;
static bool CloudBayer = true;
static float CloudDetailContribution = 0.01f;
static bool CloudHighQuality = false;

static float ColorPhiBias = 2.0f;
static float CloudResolution = 0.5f;

static bool VXAO = true;
static bool WiderSVGF = false;
static bool DITHER_SPATIAL_UPSCALE = true;

static bool PointVolumetricsToggled = false;

static float InitialTraceResolution = 1.0f;
static float DiffuseTraceResolution = 0.250f; 

static float ShadowTraceResolution = 0.500f;
static float ReflectionTraceResolution = 0.250; 
static float SSAOResolution = 0.35f;
static float RTAOResolution = 0.125f;
static float VolumetricResolution = 0.5f;

static float SunTick = 50.0f;
static float DiffuseLightIntensity = 1.2f;
static float LensFlareIntensity = 0.075f;
static float BloomQuality = 0.25f;

static bool SoftShadows = true;

static bool ReprojectReflectionsToScreenSpace = true;

static int DiffuseSPP = 3; 
static int ReflectionSPP = 2;

// Alpha test : 
static bool ShouldAlphaTest = false;
static bool ShouldAlphaTestShadows = false;


static bool TAA = true;
static bool Bloom = true;
static bool USE_SVGF = true;
static bool DO_VARIANCE_SPATIAL = true;
static bool DO_SVGF_SPATIAL = true;

static bool BrutalFXAA = true;

static bool GodRays = false;
static bool FakeGodRays = false;
static bool RoughReflections = true;
static bool DenoiseReflections = true;
static bool RenderParticles = true;
static bool Bloom_HQ = false;

static bool LensFlare = false;
static bool SSAO = false;
static bool RTAO = false;
static bool POM = false;
static bool HighQualityPOM = false;

static bool CheckerboardClouds = true;
static bool AmplifyNormalMap = true;

static int GodRaysStepCount = 12;

static bool AutoExposure = false;
static bool ExponentialFog = false;

static glm::vec3 SunDirection;
static glm::vec3 MoonDirection;

static VoxelRT::World* world = nullptr;
static bool ModifiedWorld = false;
static VoxelRT::FPSCamera& MainCamera = MainPlayer.Camera;
static VoxelRT::OrthographicCamera OCamera(0.0f, 800.0f, 0.0f, 600.0f);

static float Frametime;
static float DeltaTime;

float VoxelRT_VolumeMultiplier = 1.0f;

static float DeltaSum = 0.0f;

static float PointVolumetricsScale = 0.1f; // 0.1 * 0.1 = 1/100th the pixels :p

class RayTracerApp : public VoxelRT::Application
{
public:

	RayTracerApp()
	{
		m_Width = 800;
		m_Height = 600;
	}

	void OnUserCreate(double ts) override
	{

	}

	void OnUserUpdate(double ts) override
	{

	}

	void OnImguiRender(double ts) override
	{
		if (ImGui::Begin("Settings"))
		{
			ImGui::Checkbox("Use SVGF? (Uses Atrous if disabled, SVGF recommended) ", &USE_SVGF);
			ImGui::Checkbox("DO_SVGF_SPATIAL ", &DO_SVGF_SPATIAL);
			ImGui::Checkbox("DO_VARIANCE_SVGF_SPATIAL ", &DO_VARIANCE_SPATIAL);
			ImGui::Checkbox("WIDE_SVGF_SPATIAL ", &WiderSVGF);
			ImGui::Checkbox("CAS (Contrast Adaptive Sharpening)", &ContrastAdaptiveSharpening);
			ImGui::Checkbox("BAYER 4x4 DITHER SPATIAL UPSCALE", &DITHER_SPATIAL_UPSCALE);
			ImGui::SliderFloat("CAS SharpenAmount", &CAS_SharpenAmount, 0.0f, 0.8f);
			ImGui::Checkbox("Jitter Projection Matrix For TAA? (small issues, right now :( ) ", &JitterSceneForTAA);
			ImGui::Checkbox("VERY VERY WIP! : Point Light Volumetrics?", &PointVolumetricsToggled);
			ImGui::SliderFloat("SVGF : Color Phi Bias", &ColorPhiBias, 0.5f, 6.0f);

			ImGui::NewLine();
			ImGui::NewLine();
			ImGui::Text("Player Position : %f, %f, %f", MainCamera.GetPosition().x, MainCamera.GetPosition().y, MainCamera.GetPosition().z);
			ImGui::Text("Camera Front : %f, %f, %f", MainCamera.GetFront().x, MainCamera.GetFront().y, MainCamera.GetFront().z);
			ImGui::SliderFloat("VOL Resolution", &PointVolumetricsScale, 0.05f, 1.0f);
			ImGui::SliderFloat("Initial Trace Resolution", &InitialTraceResolution, 0.1f, 1.0f);
			ImGui::SliderFloat("Diffuse Trace Resolution ", &DiffuseTraceResolution, 0.1f, 1.25f);
			ImGui::SliderFloat("Shadow Trace Resolution ", &ShadowTraceResolution, 0.1f, 1.25f);
			ImGui::SliderFloat("Reflection Trace Resolution ", &ReflectionTraceResolution, 0.1f, 0.8f);
			ImGui::SliderFloat("SSAO Render Resolution ", &SSAOResolution, 0.1f, 0.9f);
			ImGui::SliderFloat("Sun Time ", &SunTick, 0.1f, 256.0f);
			ImGui::SliderFloat("Lens Flare Intensity ", &LensFlareIntensity, 0.05f, 1.25f);
			ImGui::SliderFloat("RTAO Resolution ", &RTAOResolution, 0.1f, 0.9f);
			ImGui::SliderFloat("Bloom Resolution ", &BloomQuality, 0.25f, 0.5f);
			ImGui::SliderInt("God ray raymarch step count", &GodRaysStepCount, 8, 64);
			ImGui::SliderFloat("Diffuse Light Intensity ", &DiffuseLightIntensity, 0.05f, 1.25f);
			ImGui::SliderInt("Diffuse Trace SPP", &DiffuseSPP, 1, 32);
			ImGui::SliderInt("Reflection Trace SPP", &ReflectionSPP, 1, 16);
		//	ImGui::Checkbox("Brutal FXAA? (Smoother edges, might overblur.)", &BrutalFXAA);
			ImGui::Checkbox("Use screen space data for reflections?", &ReprojectReflectionsToScreenSpace);
			
			//ImGui::Checkbox("Do second spatial filtering pass (For indirect, more expensive, reduces noise) ?", &DoSecondSpatialPass);
			
			ImGui::Checkbox("Contact Hardening Shadows?", &SoftShadows);
			ImGui::Checkbox("Rough reflections?", &RoughReflections);
			ImGui::Checkbox("High Quality Bloom?", &Bloom_HQ);
			ImGui::Checkbox("Denoise reflections?", &DenoiseReflections);
			ImGui::Checkbox("Particles?", &RenderParticles);
			ImGui::Checkbox("Amplify normal map?", &AmplifyNormalMap);

			ImGui::Checkbox("RTAO_1?", &VXAO);
			ImGui::Checkbox("Do RTAO (RTAO_1 not availible when SVGF filtering is disabled!)", &RTAO);


			ImGui::Checkbox("High Quality POM?", &HighQualityPOM);
			ImGui::Checkbox("Temporal Anti Aliasing", &TAA);
			ImGui::Checkbox("Volumetric Clouds?", &CloudsEnabled);
			ImGui::Checkbox("High Quality Clouds? (Doubles the ray march step count)", &CloudHighQuality);
			ImGui::Checkbox("Use Bayer Dither for clouds? (Uses white noise if disabled)", &CloudBayer);
			ImGui::SliderFloat("Volumetric Cloud Coverage", &CloudCoverage, 0.01f, 0.6f);
			//ImGui::SliderFloat("Volumetric Cloud Detail Contribution", &CloudDetailContribution, 0.0f, 1.5f);
			ImGui::SliderFloat("Volumetric Cloud Resolution (Effectively halved when checkering is enabled)", &CloudResolution, 0.1f, 0.5f);

			ImGui::Checkbox("Checkerboard clouds?", &CheckerboardClouds);
			ImGui::Checkbox("Lens Flare?", &LensFlare);
			ImGui::Checkbox("(Implementation - 1) (WIP) God Rays? (Slower)", &GodRays);
			ImGui::Checkbox("(Implementation - 2) (WIP) God Rays? (faster, more crisp, Adjust the step count in the menu)", &FakeGodRays);
			ImGui::Checkbox("Exponential Fog?", &ExponentialFog);
			ImGui::Checkbox("Bloom (Expensive!) ?", &Bloom);

			ImGui::NewLine();
			ImGui::NewLine();
			ImGui::Text("WIP and not-recommended stuff : ");
			ImGui::NewLine();
			ImGui::NewLine();

			ImGui::Checkbox("Screen Space Ambient Occlusion? (VXAO/RTAO recommended)", &SSAO);
			ImGui::Checkbox("Auto Exposure (Very very WIP!) ?", &AutoExposure);
			ImGui::Checkbox("Alpha Test? (WIP, has a few artifacts.) ", &ShouldAlphaTest);
			ImGui::Checkbox("Alpha Test Shadows? (WIP, has a few artifacts.)", &ShouldAlphaTestShadows);
			ImGui::Checkbox("POM? (VERY WORK IN PROGRESS, \
				The textures adapted from minecraft resource packs use a d ifferent parallax representation that needs to be handles)", &POM);
		} ImGui::End();

		if (ImGui::Begin("Other Settings and properties"))
		{
			if (world) {

				std::string s = MainPlayer.m_isOnGround ? "Yes" : "No";
				std::string s1 = "Air, not grounded.";

				//if (MainPlayer.m_isOnGround) 
				{

					glm::ivec3 Idx = glm::ivec3(glm::floor(MainPlayer.m_Position));
					Idx.y -= 2;

					if (Idx.x > 0 && Idx.x < WORLD_SIZE_X - 1 && 
						Idx.y > 0 && Idx.y < WORLD_SIZE_Y - 1 && 
						Idx.z > 0 && Idx.z < WORLD_SIZE_Z - 1)
					{
						auto blockat = world->GetBlock((uint16_t)Idx.x, (uint16_t)Idx.y, (uint16_t)Idx.z);
						s1 = blockat.block > 0 ? VoxelRT::BlockDatabase::GetBlockName(blockat.block) : s1;
					}

				}

				ImGui::Text("Stood On Block : %s", s1.c_str());
				ImGui::SliderFloat("Volume", &VoxelRT_VolumeMultiplier, 0.0f, 3.5f);
			}

			ImGui::NewLine();
			ImGui::NewLine();

			ImGui::NewLine();
			ImGui::Text("--- Spec Presets ---\n");

			if (ImGui::Button("LOW") == true)
			{
				InitialTraceResolution = 1.0f;
				ShadowTraceResolution = 0.5f;
				DiffuseSPP = 3;
				ColorPhiBias = 2.0f;
				ReflectionTraceResolution = 0.25f;
				DiffuseTraceResolution = 0.25f;
				RTAO = false;
			}

			if (ImGui::Button("MEDIUM") == true)
			{
				InitialTraceResolution = 1.0f;
				ShadowTraceResolution = 0.75f;
				DiffuseSPP = 8;
				ColorPhiBias = 3.5f;
				ReflectionTraceResolution = 0.25f;
				DiffuseTraceResolution = 0.25f;
				RTAO = false;
			}

			if (ImGui::Button("HIGH") == true) {
				InitialTraceResolution = 1.0f;
				ShadowTraceResolution = 0.75f;
				DiffuseSPP = 2;
				ColorPhiBias = 3.25f;
				ReflectionTraceResolution = 0.5f;
				DiffuseTraceResolution = 0.5f;
				RTAO = false;
			}

			if (ImGui::Button("INSANE") == true) {
				InitialTraceResolution = 1.0f;
				ShadowTraceResolution = 1.0f;
				ReflectionTraceResolution = 0.5f;
				DiffuseTraceResolution = 0.5f;
				DiffuseSPP = 4;
				ColorPhiBias = 3.5f;
				RTAO = false;
			}

			ImGui::NewLine();
			ImGui::NewLine();

			ImGui::SliderFloat("Mouse Sensitivity", &MainPlayer.Sensitivity, 0.025f, 1.0f);
			ImGui::SliderFloat("Player Speed", &MainPlayer.Speed, 0.025f, 1.0f);
			ImGui::Checkbox("VSync", &VSync);

			if (ImGui::Button("Reset"))
			{
				MainPlayer.Sensitivity = 0.25f;
				MainPlayer.Speed = 0.045f;
				VoxelRT_VolumeMultiplier = 1.0f;
				VSync = false;
			}
		} ImGui::End();
	}

	void OnEvent(VoxelRT::Event e) override
	{
		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_SPACE)
		{
			MainPlayer.Jump();
		}

		if (e.type == VoxelRT::EventTypes::MouseMove && GetCursorLocked())
		{
			MainCamera.UpdateOnMouseMovement(GetCursorX(), GetCursorY());
		}

		if (e.type == VoxelRT::EventTypes::MousePress && e.button == GLFW_MOUSE_BUTTON_LEFT && this->GetCursorLocked())
		{
			if (world)
			{
				world->Raycast(0, MainCamera.GetPosition(), MainCamera.GetFront(), MainPlayer.m_Velocity, !MainPlayer.m_isOnGround, DeltaTime);
				ModifiedWorld = true;
			}
		}

		if (e.type == VoxelRT::EventTypes::MousePress && e.button == GLFW_MOUSE_BUTTON_RIGHT && this->GetCursorLocked())
		{
			if (world)
			{
				world->Raycast(1, MainCamera.GetPosition(), MainCamera.GetFront(), MainPlayer.m_Velocity, !MainPlayer.m_isOnGround, DeltaTime);
				ModifiedWorld = true;
			}
		}

		if (e.type == VoxelRT::EventTypes::MousePress && e.button == GLFW_MOUSE_BUTTON_MIDDLE && this->GetCursorLocked())
		{
			if (world)
			{
				world->Raycast(2, MainCamera.GetPosition(), MainCamera.GetFront(), MainPlayer.m_Velocity, !MainPlayer.m_isOnGround, DeltaTime);
			}
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_F1)
		{
			this->SetCursorLocked(!this->GetCursorLocked());
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_F10)
		{
			std::cout << "\n\n--REUPLOADED VOLUMETRIC VOLUME TO GPU--\n\n";
			VoxelRT::Volumetrics::Reupload();
			PointVolumetricsToggled = !PointVolumetricsToggled;
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_V)
		{
			VSync = !VSync;
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_F)
		{
			MainPlayer.Freefly = !MainPlayer.Freefly;
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_Q)
		{
			world->ChangeCurrentlyHeldBlock(true);
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_E)
		{
			world->ChangeCurrentlyHeldBlock(false);
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_C)
		{
			MainPlayer.DisableCollisions = !MainPlayer.DisableCollisions;
		}

		if (e.type == VoxelRT::EventTypes::KeyPress && e.key == GLFW_KEY_ESCAPE)
		{
			VoxelRT::SaveWorld(world, world->m_Name);
			delete world;
			exit(0);
		}

		if (e.type == VoxelRT::EventTypes::WindowResize)
		{
			MainCamera.SetAspect((float)e.wx / (float)e.wy);
			OCamera.SetProjection(0.0f, e.wx, 0.0f, e.wy);
		}




		
	}

};

GLClasses::Framebuffer InitialTraceFBO_1(16, 16, { {GL_R16F, GL_RED, GL_FLOAT, true, true}, {GL_RED, GL_RED, GL_UNSIGNED_BYTE, false, false}, {GL_RED, GL_RED, GL_UNSIGNED_BYTE, false, false} }, false);
GLClasses::Framebuffer InitialTraceFBO_2(16, 16, { {GL_R16F, GL_RED, GL_FLOAT, true, true}, {GL_RED, GL_RED, GL_UNSIGNED_BYTE, false, false}, {GL_RED, GL_RED, GL_UNSIGNED_BYTE, false, false} }, false);




GLClasses::Framebuffer DiffuseTraceFBO(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_R16F, GL_RED, GL_FLOAT }, { GL_RED, GL_RED, GL_UNSIGNED_BYTE } }, false);
GLClasses::Framebuffer DiffuseTemporalFBO1(16, 16, {{ GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_RGB16F, GL_RGB, GL_FLOAT } , { GL_RED, GL_RED, GL_UNSIGNED_BYTE } }, false);
GLClasses::Framebuffer DiffuseTemporalFBO2(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_RGB16F, GL_RGB, GL_FLOAT } , { GL_RED, GL_RED, GL_UNSIGNED_BYTE } }, false);
GLClasses::Framebuffer DiffuseDenoiseFBO(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } , { GL_R16F, GL_RED, GL_FLOAT } , { GL_RED, GL_RED, GL_UNSIGNED_BYTE } }, false);
GLClasses::Framebuffer DiffuseDenoisedFBO2(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } , { GL_R16F, GL_RED, GL_FLOAT } , { GL_RED, GL_RED, GL_UNSIGNED_BYTE } }, false);
GLClasses::Framebuffer VarianceFBO(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_R16F, GL_RED, GL_FLOAT } }, false);



GLClasses::Framebuffer VolumetricsCompute(16, 16, { { GL_RGB16F, GL_RGB, GL_FLOAT } }, false, false);
GLClasses::Framebuffer VolumetricsComputeBlurred(16, 16, { { GL_RGB16F, GL_RGB, GL_FLOAT } }, false, false);

GLClasses::Framebuffer PostProcessingFBO(16, 16, { GL_RGB16F, GL_RGB, GL_FLOAT }, false);



GLClasses::Framebuffer ReflectionTraceFBO_1(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_R16F, GL_RED, GL_FLOAT } }, false);
GLClasses::Framebuffer ReflectionTraceFBO_2(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT }, { GL_R16F, GL_RED, GL_FLOAT } }, false);
GLClasses::Framebuffer ReflectionTemporalFBO_1(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } }, false);
GLClasses::Framebuffer ReflectionTemporalFBO_2(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } }, false);
GLClasses::Framebuffer ReflectionDenoised_1(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } }, false);
GLClasses::Framebuffer ReflectionDenoised_2(16, 16, { { GL_RGBA16F, GL_RGBA, GL_FLOAT }, { GL_RG16F, GL_RG, GL_FLOAT } }, false);

GLClasses::Framebuffer ShadowRawTrace(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, false),
ShadowTemporalFBO_1(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, false), ShadowTemporalFBO_2(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, false),
ShadowFiltered(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, false);




void VoxelRT::MainPipeline::StartPipeline()
{
	using std::chrono::high_resolution_clock;
	using std::chrono::duration_cast;
	using std::chrono::duration;
	using std::chrono::milliseconds;

	std::vector<glm::ivec3> LightLocations;

	RayTracerApp app;
	app.Initialize();
	VoxelRT::BlockDatabase::Initialize();

	bool gen_type = 0;

	std::string world_name;

	world = new VoxelRT::World();

	do {
		std::cout << "\nEnter the name of your world : ";
		std::cin >> world_name;
	} while (!VoxelRT::FilenameValid(world_name));

	world->m_Name = world_name;

	if (!LoadWorld(world, world_name, LightLocations))
	{
		std::cout << "\nWhat type of world would you like to generate? (FLAT = 0, PLAINS = 1) : ";
		std::cin >> gen_type;
		std::cout << "\n\n";

		GenerateWorld(world, gen_type);
	}

	int HardwareProfile = 0;

	std::cout << "\nHardware Spec? (0 -> Low, 1 -> Medium, 2 -> High, 3 -> Insane) : ";
	std::cin >> HardwareProfile;

	if (HardwareProfile == 0)
	{
		// //
	}

	if (HardwareProfile == 1)
	{
		InitialTraceResolution = 1.0f;
		ShadowTraceResolution = 0.75f;
		DiffuseSPP = 8;
		ColorPhiBias = 3.5f;
		ReflectionTraceResolution = 0.25f;
		DiffuseTraceResolution = 0.25f;
		RTAO = false;
	}

	if (HardwareProfile == 2)
	{
		InitialTraceResolution = 1.0f;
		ShadowTraceResolution = 0.75f;
		DiffuseSPP = 2;
		ColorPhiBias = 3.25f;
		ReflectionTraceResolution = 0.5f;
		DiffuseTraceResolution = 0.5f;
		RTAO = false;
	}

	if (HardwareProfile == 3)
	{
		InitialTraceResolution = 1.0f;
		ShadowTraceResolution = 1.0f;
		ReflectionTraceResolution = 0.5f;
		DiffuseTraceResolution = 0.5f;
		DiffuseSPP = 4;
		ColorPhiBias = 3.5f;
		RTAO = false;
	}

	// Initialize world, df generator etc 
	world->Buffer();
	world->InitializeDistanceGenerator();
	world->GenerateDistanceField();

	// Initialize sound engine

	std::cout << "\n\n";
	std::cout << "Initializing Sound Engine..\n";
	SoundManager::InitializeSoundManager();
	std::cout << "\nInitialized Sound Engine.";
	std::cout << "\n\n";


	// Create and compile shaders 
	ShaderManager::CreateShaders();


	VoxelRT::Renderer2D RendererUI;
	GLClasses::VertexBuffer VBO;
	GLClasses::VertexArray VAO;
	GLClasses::Shader& InitialTraceShader = ShaderManager::GetShader("INITIAL_TRACE");
	GLClasses::Shader& FinalShader = ShaderManager::GetShader("FINAL_SHADER");
	GLClasses::Shader& DiffuseTraceShader = ShaderManager::GetShader("DIFFUSE_TRACE");
	GLClasses::Shader& MainTemporalFilter = ShaderManager::GetShader("MAIN_TEMPORAL_FILER");
	GLClasses::Shader& DenoiseFilter = ShaderManager::GetShader("SMART_DENOISER");
	GLClasses::Shader& ColorShader = ShaderManager::GetShader("COLOR_SHADER");
	GLClasses::Shader& PostProcessingShader = ShaderManager::GetShader("POST_PROCESS");
	GLClasses::Shader& TemporalAAShader = ShaderManager::GetShader("TEMPORAL_AA");
	GLClasses::Shader& ShadowTraceShader = ShaderManager::GetShader("SHADOW_TRACE");
	GLClasses::Shader& ReflectionTraceShader = ShaderManager::GetShader("REFLECTION_TRACE");
	GLClasses::Shader& SSAOShader = ShaderManager::GetShader("SSAO");
	GLClasses::Shader& SSAO_Blur = ShaderManager::GetShader("SSAO_BLUR");
	GLClasses::Shader& SimpleDownsample = ShaderManager::GetShader("SIMPLE_DOWNSAMPLE");
	GLClasses::Shader& LumaAverager = ShaderManager::GetShader("LUMA_AVERAGER");
	GLClasses::Shader& VolumetricScattering = ShaderManager::GetShader("VOLUMETRIC_SCATTERING");
	GLClasses::Shader& BilateralBlur = ShaderManager::GetShader("BILATERAL_BLUR");
	GLClasses::Shader& ReflectionDenoiser = ShaderManager::GetShader("REFLECTION_DENOISER");
	GLClasses::Shader& RTAOShader = ShaderManager::GetShader("RTAO");
	GLClasses::Shader& GaussianSpatialFilter = ShaderManager::GetShader("GAUSSIAN_SPATIAL_FILTER");
	GLClasses::Shader& AtrousSpatialFilter = ShaderManager::GetShader("ATROUS_SPATIAL_FILTER");
	GLClasses::Shader& SpatialInitial = ShaderManager::GetShader("SPATIAL_INITIAL");
	GLClasses::Shader& SpecularTemporalFilter = ShaderManager::GetShader("SPECULAR_TEMPORAL");
	GLClasses::Shader& DefaultCheckerboardReconstructor = ShaderManager::GetShader("CHECKER_RECONSTRUCT");
	GLClasses::Shader& SpecularCheckerboardReconstructor = ShaderManager::GetShader("SPECULAR_CHECKER_RECONSTRUCT");
	GLClasses::Shader& ShadowFilter = ShaderManager::GetShader("SHADOW_FILTER");
	GLClasses::Shader& VarianceEstimator = ShaderManager::GetShader("VARIANCE_ESTIMATOR");
	GLClasses::Shader& PointVolumetrics = ShaderManager::GetShader("VOLUMETRICS_COMPUTE");
	GLClasses::Shader& Gaussian9TapOptimized = ShaderManager::GetShader("GAUSSIAN_9TAP_OPTIMIZED");
	GLClasses::Shader& Gaussian5TapOptimized = ShaderManager::GetShader("GAUSSIAN_5TAP_OPTIMIZED");
	
	// wip.
	GLClasses::Shader& SVGF_Temporal = ShaderManager::GetShader("SVGF_TEMPORAL");
	GLClasses::Shader& SVGF_Spatial = ShaderManager::GetShader("SVGF_SPATIAL");
	GLClasses::Shader& SVGF_Variance = ShaderManager::GetShader("SVGF_VARIANCE");


	GLClasses::Shader& CAS_Shader = ShaderManager::GetShader("CONTRAST_ADAPTIVE_SHARPENING");

	GLClasses::TextureArray BlueNoise;

	
	VoxelRT::ColorPassFBO ColoredFBO;

	GLClasses::Framebuffer FXAA_Final(16, 16, { GL_RGB16F, GL_RGB, GL_FLOAT }, true);

	GLClasses::Framebuffer TAAFBO1(16, 16, { GL_RGB16F, GL_RGB, GL_FLOAT }, true);
	GLClasses::Framebuffer TAAFBO2(16, 16, { GL_RGB16F, GL_RGB, GL_FLOAT }, true);
	GLClasses::Framebuffer DownsampledFBO(16, 16, { GL_RGBA16F, GL_RGBA, GL_FLOAT }, false);
	GLClasses::Framebuffer AverageLumaFBO(16, 16, { GL_RGBA16F, GL_RGBA, GL_FLOAT }, false);
	GLClasses::Framebuffer VolumetricFBO(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true);
	GLClasses::Framebuffer BlurredVolumetricFBO(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true);
	GLClasses::Framebuffer RTAO_FBO(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true), RTAO_TemporalFBO_1(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true), RTAO_TemporalFBO_2(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true);
	VoxelRT::BloomFBO BloomFBO(16, 16);
	GLClasses::Framebuffer SSAOFBO(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true);
	GLClasses::Framebuffer SSAOBlurred(16, 16, { GL_RED, GL_RED, GL_UNSIGNED_BYTE }, true);

	glm::mat4 CurrentProjection, CurrentView;
	glm::mat4 PreviousProjection, PreviousView;
	glm::mat4 ShadowProjection, ShadowView;
	glm::mat4 ReflectionProjection, ReflectionView;
	glm::vec3 CurrentPosition, PreviousPosition;

	VoxelRT::AtmosphereRenderMap Skymap(64);
	VoxelRT::AtmosphereRenderer AtmosphereRenderer;

	BlueNoiseDataSSBO BlueNoise_SSBO;

	GLClasses::Texture Crosshair;
	GLClasses::Texture BluenoiseTexture;
	GLClasses::Texture BluenoiseHighResTexture;
	GLClasses::Texture PlayerSprite;
	Crosshair.CreateTexture("Res/Misc/crosshair.png", false);
	BluenoiseTexture.CreateTexture("Res/Misc/blue_noise.png", false);
	BluenoiseHighResTexture.CreateTexture("Res/Misc/BluenoiseHighRes.png", false);
	PlayerSprite.CreateTexture("Res/Misc/player.png", false, true);

	BlockDataSSBO BlockDataStorageBuffer;
	BlockDataStorageBuffer.CreateBuffers();
	
	BlueNoise.CreateArray({
		"Res/Misc/BL_0.png",
		"Res/Misc/BL_1.png",
		"Res/Misc/BL_2.png",
		"Res/Misc/BL_3.png"
		}, { 256, 256 }, true, false);

	float Vertices[] =
	{
		-1.0f,  1.0f,  0.0f, 1.0f, -1.0f, -1.0f,  0.0f, 0.0f,
		 1.0f, -1.0f,  1.0f, 0.0f, -1.0f,  1.0f,  0.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,  1.0f,  1.0f,  1.0f, 1.0f
	};

	VAO.Bind();
	VBO.Bind();
	VBO.BufferData(sizeof(Vertices), Vertices, GL_STATIC_DRAW);
	VBO.VertexAttribPointer(0, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), 0);
	VBO.VertexAttribPointer(1, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
	VAO.Unbind();

	app.SetCursorLocked(true);

	glDisable(GL_BLEND);

	MainCamera.SetPosition(glm::vec3(WORLD_SIZE_X / 2, 70, WORLD_SIZE_Z / 2));

	BloomRenderer::Initialize();
	AverageLumaFBO.SetSize(1, 1);

	Clouds::CloudRenderer::Initialize();

	auto* InitialTraceFBO = &InitialTraceFBO_1;
	auto* InitialTraceFBOPrev = &InitialTraceFBO_2;

	glm::vec3 StrongerLightDirection;

	GLfloat PreviousLuma = 3.0f;

	float CameraExposure = 1.0f;
	float PrevCameraExposure = 1.0f;


	Frametime = glfwGetTime();

	bool UpdatePlayerCollision = true;

	GenerateJitterStuff();


	// Volumetricssss 
	Volumetrics::CreateVolume(world, BlockDataStorageBuffer.GetSSBO(), BlockDatabase::GetTextureArray());
	for (auto& e : LightLocations) {
		uint8_t block_at = world->GetBlock(e).block;
		Volumetrics::AddLightToVolume(e, block_at);
	}

	for (int i = 0; i < 3; i++) {
		Volumetrics::PropogateVolume();
	}

	while (!glfwWindowShouldClose(app.GetWindow()))
	{
		// Player update flag
		if (glfwGetWindowAttrib(app.GetWindow(), GLFW_FOCUSED) == 0) {
			UpdatePlayerCollision = false;
		}

		else {
			UpdatePlayerCollision = true;
		}

		if (app.GetCurrentFrame() < 5) {
			UpdatePlayerCollision = true;
		}


		// Sound update ->

		SoundManager::UpdatePosition(MainCamera.GetFront(), MainCamera.GetPosition(), MainCamera.GetUp());






		// Jitter



		// Tick the sun and moon
		float time_angle = SunTick * 2.0f;
		glm::mat4 sun_rotation_matrix;

		sun_rotation_matrix = glm::rotate(glm::mat4(1.0f), glm::radians(time_angle), glm::vec3(0.0f, 0.0f, 1.0f));
		SunDirection = glm::vec3(sun_rotation_matrix * glm::vec4(1.0f));
		MoonDirection = glm::vec3(-SunDirection.x, -SunDirection.y, SunDirection.z);
		StrongerLightDirection = -SunDirection.y < 0.01f ? SunDirection : MoonDirection;

		glfwSwapInterval((int)VSync);

		float PADDED_WIDTH = app.GetWidth() + 16.0f;
		float PADDED_HEIGHT = app.GetHeight() + 16.0f;

		

		// Resize the framebuffers
		{
			
			// Without padding!
			FXAA_Final.SetSize(app.GetWidth(), app.GetHeight());



			InitialTraceFBO_1.SetSize(floor(PADDED_WIDTH * InitialTraceResolution), floor(PADDED_HEIGHT * InitialTraceResolution));
			InitialTraceFBO_2.SetSize(floor(PADDED_WIDTH * InitialTraceResolution), floor(PADDED_HEIGHT * InitialTraceResolution));

			VolumetricsCompute.SetSize(floor(PADDED_WIDTH * PointVolumetricsScale), floor(PADDED_HEIGHT * PointVolumetricsScale));
			VolumetricsComputeBlurred.SetSize(floor(PADDED_WIDTH * PointVolumetricsScale), floor(PADDED_HEIGHT * PointVolumetricsScale));

			float DiffuseResolution2 = DiffuseTraceResolution;
			DiffuseTraceFBO.SetSize(PADDED_WIDTH * DiffuseTraceResolution, PADDED_HEIGHT * DiffuseTraceResolution);
			DiffuseTemporalFBO1.SetSize(PADDED_WIDTH * DiffuseResolution2, PADDED_HEIGHT * DiffuseResolution2);
			DiffuseTemporalFBO2.SetSize(PADDED_WIDTH * DiffuseResolution2, PADDED_HEIGHT * DiffuseResolution2);
			DiffuseDenoiseFBO.SetSize(PADDED_WIDTH * DiffuseResolution2, PADDED_HEIGHT * DiffuseResolution2);
			DiffuseDenoisedFBO2.SetSize(PADDED_WIDTH * DiffuseResolution2, PADDED_HEIGHT * DiffuseResolution2);


			if (TAA)
			{
				TAAFBO1.SetSize(PADDED_WIDTH, PADDED_HEIGHT);
				TAAFBO2.SetSize(PADDED_WIDTH, PADDED_HEIGHT);
			}

			DownsampledFBO.SetSize(PADDED_WIDTH * 0.125f, PADDED_HEIGHT * 0.125f);
			BloomFBO.SetSize(PADDED_WIDTH * BloomQuality, PADDED_HEIGHT * BloomQuality);
			VarianceFBO.SetSize(PADDED_WIDTH * DiffuseTraceResolution, PADDED_HEIGHT * DiffuseTraceResolution);
			PostProcessingFBO.SetSize(PADDED_WIDTH, PADDED_HEIGHT);
			ColoredFBO.SetDimensions(PADDED_WIDTH, PADDED_HEIGHT);

			ShadowRawTrace.SetSize(PADDED_WIDTH * ShadowTraceResolution, PADDED_HEIGHT * ShadowTraceResolution);
			ShadowTemporalFBO_1.SetSize(PADDED_WIDTH * ShadowTraceResolution * 1.5f, PADDED_HEIGHT * ShadowTraceResolution * 1.5f);
			ShadowTemporalFBO_2.SetSize(PADDED_WIDTH * ShadowTraceResolution * 1.5f, PADDED_HEIGHT * ShadowTraceResolution * 1.5f);
			ShadowFiltered.SetSize(PADDED_WIDTH * ShadowTraceResolution * 1.5f, PADDED_HEIGHT * ShadowTraceResolution * 1.5f);

			ReflectionTraceFBO_1.SetSize(PADDED_WIDTH * ReflectionTraceResolution, PADDED_HEIGHT * ReflectionTraceResolution);
			ReflectionTraceFBO_2.SetSize(PADDED_WIDTH * ReflectionTraceResolution, PADDED_HEIGHT * ReflectionTraceResolution);
			ReflectionTemporalFBO_1.SetSize(PADDED_WIDTH * ReflectionTraceResolution * 2.0f, PADDED_HEIGHT * ReflectionTraceResolution * 2.0f);
			ReflectionTemporalFBO_2.SetSize(PADDED_WIDTH * ReflectionTraceResolution * 2.0f, PADDED_HEIGHT * ReflectionTraceResolution * 2.0f);
			ReflectionDenoised_1.SetSize(PADDED_WIDTH * ReflectionTraceResolution * 2.0f, PADDED_HEIGHT * ReflectionTraceResolution * 2.0f);
			ReflectionDenoised_2.SetSize(PADDED_WIDTH * ReflectionTraceResolution * 2.0f, PADDED_HEIGHT * ReflectionTraceResolution * 2.0f);

			if (GodRays)
			{
				VolumetricFBO.SetSize(PADDED_WIDTH * VolumetricResolution, PADDED_HEIGHT * VolumetricResolution);
				BlurredVolumetricFBO.SetSize(PADDED_WIDTH * VolumetricResolution, PADDED_HEIGHT * VolumetricResolution);
			}

			if (SSAO)
			{
				SSAOFBO.SetSize(PADDED_WIDTH * SSAOResolution, PADDED_HEIGHT * SSAOResolution);
				SSAOBlurred.SetSize(PADDED_WIDTH * SSAOResolution, PADDED_HEIGHT * SSAOResolution);
			}

			if (RTAO)
			{
				float RTAO_Res2 = glm::max(RTAOResolution, 0.5f);
				RTAO_FBO.SetSize(PADDED_WIDTH * RTAOResolution, PADDED_HEIGHT * RTAOResolution);
				RTAO_TemporalFBO_1.SetSize(PADDED_WIDTH * RTAO_Res2, PADDED_HEIGHT * RTAO_Res2);
				RTAO_TemporalFBO_2.SetSize(PADDED_WIDTH * RTAO_Res2, PADDED_HEIGHT * RTAO_Res2);
			}
		}

		GLClasses::Framebuffer& TAAFBO = (app.GetCurrentFrame() % 2 == 0) ? TAAFBO1 : TAAFBO2;
		GLClasses::Framebuffer& PrevTAAFBO = (app.GetCurrentFrame() % 2 == 0) ? TAAFBO2 : TAAFBO1;
		GLClasses::Framebuffer& DiffuseTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? DiffuseTemporalFBO1 : DiffuseTemporalFBO2;
		GLClasses::Framebuffer& PrevDiffuseTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? DiffuseTemporalFBO2 : DiffuseTemporalFBO1;
		GLClasses::Framebuffer& ReflectionTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? ReflectionTemporalFBO_1 : ReflectionTemporalFBO_2;
		GLClasses::Framebuffer& PrevReflectionTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? ReflectionTemporalFBO_2 : ReflectionTemporalFBO_1;
		GLClasses::Framebuffer& RTAOTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? RTAO_TemporalFBO_1 : RTAO_TemporalFBO_2;
		GLClasses::Framebuffer& PrevRTAOTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? RTAO_TemporalFBO_2 : RTAO_TemporalFBO_1;
		GLClasses::Framebuffer& ShadowTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? ShadowTemporalFBO_1 : ShadowTemporalFBO_2;
		GLClasses::Framebuffer& PrevShadowTemporalFBO = (app.GetCurrentFrame() % 2 == 0) ? ShadowTemporalFBO_2 : ShadowTemporalFBO_1;
		
		GLClasses::Framebuffer& ReflectionTraceFBO = (app.GetCurrentFrame() % 2 == 0) ? ReflectionTraceFBO_1 : ReflectionTraceFBO_2;
		GLClasses::Framebuffer& PrevReflectionTraceFBO = (app.GetCurrentFrame() % 2 == 0) ? ReflectionTraceFBO_2 : ReflectionTraceFBO_1;



		if (glfwGetKey(app.GetWindow(), GLFW_KEY_F2) == GLFW_PRESS)
		{
			system("@cls");
			ShaderManager::RecompileShaders(); 
			world->m_ParticleEmitter.Recompile();
			Clouds::CloudRenderer::RecompileShaders();
			BloomRenderer::RecompileShaders();
			AtmosphereRenderer.Recompile();
			VoxelRT::Logger::Log("Recompiled!");
		}

		if (UpdatePlayerCollision) {
			MainPlayer.OnUpdate(app.GetWindow(), world, DeltaTime * 6.9f, (int)app.GetCurrentFrame(), DeltaSum);
		}

		app.OnUpdate();

		glm::mat4 TempView = PreviousView;

		PreviousProjection = CurrentProjection;
		PreviousView = CurrentView;
		PreviousPosition = CurrentPosition;
		CurrentProjection = MainCamera.GetProjectionMatrix();
		CurrentView = MainCamera.GetViewMatrix();
		CurrentPosition = MainCamera.GetPosition();

		glm::mat4 inv_view = glm::inverse(MainCamera.GetViewMatrix());
		glm::mat4 inv_projection = glm::inverse(MainCamera.GetProjectionMatrix());
		bool PlayerMoved = TempView != MainCamera.GetViewMatrix();

		if (app.GetCurrentFrame() % 3 == 0)
		{
			AtmosphereRenderer.RenderAtmosphere(Skymap, glm::normalize(SunDirection), 30, 4);
		}

		if (PreviousView != CurrentView || app.GetCurrentFrame() % 20 == 0 ||
			ModifiedWorld || JitterSceneForTAA)
		{
			// Swap the initial trace framebuffers
			InitialTraceFBO = InitialTraceFBO == &InitialTraceFBO_1 ? &InitialTraceFBO_2 : &InitialTraceFBO_1;
			InitialTraceFBOPrev = InitialTraceFBO == &InitialTraceFBO_1 ? &InitialTraceFBO_2 : &InitialTraceFBO_1;

			InitialTraceFBO->Bind();
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);

			InitialTraceShader.Use();

			glm::mat4 JitterMatrix = GetTAAJitterMatrix(app.GetCurrentFrame(), glm::vec2(floor(app.GetWidth() * InitialTraceResolution), floor(app.GetHeight() * InitialTraceResolution)));
			glm::vec2 TAAJitter = GetTAAJitter(app.GetCurrentFrame(), glm::vec2(floor(PADDED_WIDTH * InitialTraceResolution), floor(PADDED_HEIGHT * InitialTraceResolution)));


			InitialTraceShader.SetMatrix4("u_InverseView", inv_view);
			InitialTraceShader.SetMatrix4("u_InverseProjection", JitterSceneForTAA ? glm::inverse(JitterMatrix * MainCamera.GetProjectionMatrix()) : 
																 glm::inverse(MainCamera.GetProjectionMatrix()));
			InitialTraceShader.SetInteger("u_VoxelDataTexture", 0);
			InitialTraceShader.SetInteger("u_AlbedoTextures", 1);
			InitialTraceShader.SetInteger("u_DistanceFieldTexture", 2);
			InitialTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
			InitialTraceShader.SetInteger("u_VertCurrentFrame", app.GetCurrentFrame());
			InitialTraceShader.SetVector2f("u_Dimensions", glm::vec2(InitialTraceFBO->GetWidth(), InitialTraceFBO->GetHeight()));
			InitialTraceShader.SetVector2f("u_VertDimensions", glm::vec2(PADDED_WIDTH, PADDED_HEIGHT));
			InitialTraceShader.SetVector2f("u_CurrentTAAJitter", glm::vec2(TAAJitter));
			InitialTraceShader.SetBool("u_ShouldAlphaTest", ShouldAlphaTest);
			
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_3D, world->m_DataTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetTextureArray());

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_3D, world->m_DistanceFieldTexture.GetTextureID());

			BlockDataStorageBuffer.Bind(0);

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			InitialTraceFBO->Unbind();
		}

		// Diffuse tracing

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		DiffuseTraceFBO.Bind();
		glClear(GL_COLOR_BUFFER_BIT);

		DiffuseTraceShader.Use();

		DiffuseTraceShader.SetInteger("u_VoxelData", 0);
		DiffuseTraceShader.SetInteger("u_PositionTexture", 1);
		DiffuseTraceShader.SetInteger("u_NormalTexture", 2);
		DiffuseTraceShader.SetInteger("u_Skymap", 3);
		DiffuseTraceShader.SetInteger("u_BlockNormalTextures", 4);
		DiffuseTraceShader.SetInteger("u_BlockAlbedoTextures", 6);
		DiffuseTraceShader.SetInteger("u_BlueNoiseTextures", 7);
		DiffuseTraceShader.SetInteger("u_BlockPBRTextures", 8);
		DiffuseTraceShader.SetInteger("u_BlockEmissiveTextures", 11);
		DiffuseTraceShader.SetInteger("u_DistanceFieldTexture", 13);

		DiffuseTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
		DiffuseTraceShader.SetMatrix4("u_InverseView", inv_view);
		DiffuseTraceShader.SetMatrix4("u_InverseProjection", inv_projection);
		DiffuseTraceShader.SetVector2f("u_Dimensions", glm::vec2(DiffuseTraceFBO.GetWidth(), DiffuseTraceFBO.GetHeight()));
		DiffuseTraceShader.SetFloat("u_Time", glfwGetTime() * 1.2f);
		DiffuseTraceShader.SetFloat("u_DiffuseLightIntensity", DiffuseLightIntensity);
		DiffuseTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
		DiffuseTraceShader.SetVector3f("u_ViewerPosition", MainCamera.GetPosition());
		DiffuseTraceShader.SetVector3f("u_SunDirection", SunDirection);
		DiffuseTraceShader.SetVector3f("u_MoonDirection", MoonDirection);

		DiffuseTraceShader.SetMatrix4("u_ShadowProjection", ShadowProjection);
		DiffuseTraceShader.SetMatrix4("u_ShadowView", ShadowView);
		DiffuseTraceShader.SetInteger("u_ShadowMap", 9);
		DiffuseTraceShader.SetInteger("u_BlueNoiseTexture", 10);
		DiffuseTraceShader.SetInteger("u_SPP", DiffuseSPP);
		DiffuseTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());

		DiffuseTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
		DiffuseTraceShader.SetInteger("u_CurrentFrameMod512", glm::clamp((int)app.GetCurrentFrame() % 512, 0, 524));

		DiffuseTraceShader.SetMatrix4("u_VertInverseView", inv_view);
		DiffuseTraceShader.SetMatrix4("u_VertInverseProjection", inv_projection);
		DiffuseTraceShader.SetMatrix4("u_InverseView", inv_view);
		DiffuseTraceShader.SetMatrix4("u_InverseProjection", inv_projection);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, world->m_DataTexture.GetTextureID());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_CUBE_MAP, Skymap.GetTexture());

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetNormalTextureArray());

		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetTextureArray());

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D_ARRAY, BlueNoise.GetTextureArray());

		glActiveTexture(GL_TEXTURE8);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetPBRTextureArray());

		glActiveTexture(GL_TEXTURE9);
		glBindTexture(GL_TEXTURE_2D, ShadowRawTrace.GetTexture());

		glActiveTexture(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

		glActiveTexture(GL_TEXTURE11);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetEmissiveTextureArray());

		glActiveTexture(GL_TEXTURE13);
		glBindTexture(GL_TEXTURE_3D, world->m_DistanceFieldTexture.GetTextureID());

		BlockDataStorageBuffer.Bind(0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, BlueNoise_SSBO.m_SSBO);

		VAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		VAO.Unbind();

		DiffuseTraceFBO.Unbind();


		if (USE_SVGF)
		{
				// Temporal filter 

				DiffuseTemporalFBO.Bind();
				SVGF_Temporal.Use();

				SVGF_Temporal.SetInteger("u_CurrentCoCg", 0);
				SVGF_Temporal.SetInteger("u_CurrentPositionTexture", 1);
				SVGF_Temporal.SetInteger("u_PrevCoCg", 2);
				SVGF_Temporal.SetInteger("u_PreviousPositionTexture", 3);


				SVGF_Temporal.SetInteger("u_CurrentSH", 4);
				SVGF_Temporal.SetInteger("u_PreviousSH", 5);

				SVGF_Temporal.SetInteger("u_CurrentNormalTexture", 6);
				SVGF_Temporal.SetInteger("u_PreviousNormalTexture", 7);
				SVGF_Temporal.SetInteger("u_PreviousUtility", 8);
				SVGF_Temporal.SetInteger("u_NoisyLuminosity", 9);

				SVGF_Temporal.SetInteger("u_CurrentBlockIDTexture", 10);
				SVGF_Temporal.SetInteger("u_PrevBlockIDTexture", 11);


				//u_CurrentAO;
				//u_PreviousAO;
				SVGF_Temporal.SetInteger("u_CurrentAO", 12);
				SVGF_Temporal.SetInteger("u_PreviousAO", 13);


				SVGF_Temporal.SetBool("u_DiffuseTemporal", true);
				SVGF_Temporal.SetBool("u_ShadowTemporal", false);


				SVGF_Temporal.SetMatrix4("u_Projection", CurrentProjection);
				SVGF_Temporal.SetMatrix4("u_View", CurrentView);
				SVGF_Temporal.SetMatrix4("u_PrevProjection", PreviousProjection);
				SVGF_Temporal.SetMatrix4("u_PrevView", PreviousView);

				SVGF_Temporal.SetFloat("u_MinimumMix", 0.0f);
				SVGF_Temporal.SetFloat("u_MaximumMix", 0.96f);
				SVGF_Temporal.SetInteger("u_TemporalQuality", 0); // No clamping!
				SVGF_Temporal.SetBool("u_ReflectionTemporal", false);
				SVGF_Temporal.SetFloat("u_ClampBias", 0.025f);
				SVGF_Temporal.SetVector3f("u_PrevCameraPos", PreviousPosition);
				SVGF_Temporal.SetVector3f("u_CurrentCameraPos", MainCamera.GetPosition());
				SVGF_Temporal.SetVector2f("u_Dimensions", glm::vec2(DiffuseTemporalFBO.GetWidth(), DiffuseTemporalFBO.GetHeight()));


				SVGF_Temporal.SetMatrix4("u_VertInverseView", inv_view);
				SVGF_Temporal.SetMatrix4("u_VertInverseProjection", inv_projection);
				SVGF_Temporal.SetMatrix4("u_InverseView", inv_view);
				SVGF_Temporal.SetMatrix4("u_InverseProjection", inv_projection);
				SVGF_Temporal.SetMatrix4("u_PrevInverseProjection", glm::inverse(PreviousProjection));
				SVGF_Temporal.SetMatrix4("u_PrevInverseView", glm::inverse(PreviousView));
				//uniform mat4 u_PrevInverseProjection;
				//uniform mat4 u_PrevInverseView;
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(1));
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(1));
				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(0));
				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(0));

				glActiveTexture(GL_TEXTURE6);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));
				glActiveTexture(GL_TEXTURE7);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(1));

				glActiveTexture(GL_TEXTURE8);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(2));

				glActiveTexture(GL_TEXTURE9);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(2));

				glActiveTexture(GL_TEXTURE10);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

				glActiveTexture(GL_TEXTURE11);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(2));

				glActiveTexture(GL_TEXTURE12);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(3));
				glActiveTexture(GL_TEXTURE13);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(3));

				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();

				DiffuseTemporalFBO.Unbind();

				// Do a variance estimation pass

				VarianceFBO.Bind();
				SVGF_Variance.Use();

				SVGF_Variance.SetInteger("u_PositionTexture", 0);
				SVGF_Variance.SetInteger("u_NormalTexture", 1);
				SVGF_Variance.SetInteger("u_SH", 2);
				SVGF_Variance.SetInteger("u_CoCg", 3);
				SVGF_Variance.SetInteger("u_Utility", 4);
				SVGF_Variance.SetMatrix4("u_InverseView", inv_view);
				SVGF_Variance.SetMatrix4("u_InverseProjection", inv_projection);
				SVGF_Variance.SetMatrix4("u_VertInverseView", inv_view);
				SVGF_Variance.SetMatrix4("u_VertInverseProjection", inv_projection);
				SVGF_Variance.SetBool("DO_SPATIAL", DO_VARIANCE_SPATIAL);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, DiffuseTemporalFBO.GetTexture());

				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, DiffuseTemporalFBO.GetTexture(1));

				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, DiffuseTemporalFBO.GetTexture(2));

				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();

				VarianceFBO.Unbind();

				// Spatial filter :

				int StepSizes[5];


				if (WiderSVGF)
				{
					StepSizes[0] = 32;
					StepSizes[1] = 16;
					StepSizes[2] = 8;
					StepSizes[3] = 4;
					StepSizes[4] = 2;
				}
				
				else
				{
					StepSizes[0] = 12;
					StepSizes[1] = 8;
					StepSizes[2] = 6;
					StepSizes[3] = 4;
					StepSizes[4] = 2;
				}

				for (int i = 0; i < 5; i++)
				{
					// 1 2 1 2 1
					auto& CurrentDenoiseFBO = (i % 2 == 0) ? DiffuseDenoiseFBO : DiffuseDenoisedFBO2;
					auto& PrevDenoiseFBO = (i == 0) ? VarianceFBO :
						(i % 2 == 0) ? DiffuseDenoisedFBO2 : DiffuseDenoiseFBO;

					GLuint VarianceTexture = 0;
					GLuint AOTexture = 0;

					if (i == 0)
					{
						VarianceTexture = VarianceFBO.GetTexture(2);
					}

					else {

						if (i % 2 == 0)
						{
							VarianceTexture = DiffuseDenoisedFBO2.GetTexture(2);
						}

						else {
							VarianceTexture = DiffuseDenoiseFBO.GetTexture(2);
						}
					}

					// ao texture

					if (i == 0)
					{
						AOTexture = DiffuseTemporalFBO.GetTexture(3);
					}

					else {

						if (i % 2 == 0)
						{
							AOTexture = DiffuseDenoisedFBO2.GetTexture(3);
						}

						else {
							AOTexture = DiffuseDenoiseFBO.GetTexture(3);
						}
					}







					CurrentDenoiseFBO.Bind();
					SVGF_Spatial.Use();

					// textures :
					SVGF_Spatial.SetInteger("u_SH", 0);
					SVGF_Spatial.SetInteger("u_PositionTexture", 1);
					SVGF_Spatial.SetInteger("u_NormalTexture", 2);
					SVGF_Spatial.SetInteger("u_BlockIDTexture", 3);
					SVGF_Spatial.SetInteger("u_VarianceTexture", 4);
					SVGF_Spatial.SetInteger("u_CoCg", 5);
					SVGF_Spatial.SetInteger("u_Utility", 6);
					SVGF_Spatial.SetInteger("u_AO", 8);

					SVGF_Spatial.SetInteger("u_Step", StepSizes[i]);
					SVGF_Spatial.SetVector2f("u_Dimensions", glm::vec2(DiffuseTemporalFBO.GetWidth(), DiffuseTemporalFBO.GetHeight()));
					SVGF_Spatial.SetMatrix4("u_VertInverseView", inv_view);
					SVGF_Spatial.SetMatrix4("u_VertInverseProjection", inv_projection);
					SVGF_Spatial.SetMatrix4("u_InverseView", inv_view);
					SVGF_Spatial.SetMatrix4("u_InverseProjection", inv_projection);
					SVGF_Spatial.SetBool("u_ShouldDetailWeight", !(i >= 3));
					SVGF_Spatial.SetBool("DO_SPATIAL", DO_SVGF_SPATIAL);
					SVGF_Spatial.SetFloat("u_ColorPhiBias", ColorPhiBias);

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, PrevDenoiseFBO.GetTexture());

					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

					glActiveTexture(GL_TEXTURE2);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

					glActiveTexture(GL_TEXTURE3);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

					glActiveTexture(GL_TEXTURE4);
					glBindTexture(GL_TEXTURE_2D, VarianceTexture);

					glActiveTexture(GL_TEXTURE5);
					glBindTexture(GL_TEXTURE_2D, PrevDenoiseFBO.GetTexture(1));

					glActiveTexture(GL_TEXTURE6);
					glBindTexture(GL_TEXTURE_2D, DiffuseTemporalFBO.GetTexture(3));

					glActiveTexture(GL_TEXTURE8);
					glBindTexture(GL_TEXTURE_2D, AOTexture);

					VAO.Bind();
					glDrawArrays(GL_TRIANGLES, 0, 6);
					VAO.Unbind();
				}

				glUseProgram(0);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}


		// ATROUS WAVELET FILTER //
		else
		{
				// Temporal filter 

				DiffuseTemporalFBO.Bind();
				MainTemporalFilter.Use();

				MainTemporalFilter.SetInteger("u_CurrentColorTexture", 0);
				MainTemporalFilter.SetInteger("u_CurrentPositionTexture", 1);
				MainTemporalFilter.SetInteger("u_PreviousColorTexture", 2);
				MainTemporalFilter.SetInteger("u_PreviousFramePositionTexture", 3);


				MainTemporalFilter.SetInteger("u_CurrentSH", 4);
				MainTemporalFilter.SetInteger("u_PreviousSH", 5);
				MainTemporalFilter.SetInteger("u_NormalTexture", 8);

				MainTemporalFilter.SetInteger("u_CurrentAO", 10);
				MainTemporalFilter.SetInteger("u_PreviousAO", 11);

				MainTemporalFilter.SetBool("u_DiffuseTemporal", true);
				MainTemporalFilter.SetBool("u_ShadowTemporal", false);


				MainTemporalFilter.SetMatrix4("u_Projection", CurrentProjection);
				MainTemporalFilter.SetMatrix4("u_View", CurrentView);
				MainTemporalFilter.SetMatrix4("u_PrevProjection", PreviousProjection);
				MainTemporalFilter.SetMatrix4("u_PrevView", PreviousView);

				MainTemporalFilter.SetFloat("u_MinimumMix", 0.0f);
				MainTemporalFilter.SetFloat("u_MaximumMix", 0.96f);
				MainTemporalFilter.SetInteger("u_TemporalQuality", 0); // No clamping!
				MainTemporalFilter.SetBool("u_ReflectionTemporal", false);
				MainTemporalFilter.SetFloat("u_ClampBias", 0.025f);
				MainTemporalFilter.SetVector3f("u_PrevCameraPos", PreviousPosition);
				MainTemporalFilter.SetVector3f("u_CurrentCameraPos", MainCamera.GetPosition());


				MainTemporalFilter.SetMatrix4("u_VertInverseView", inv_view);
				MainTemporalFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
				MainTemporalFilter.SetMatrix4("u_InverseView", inv_view);
				MainTemporalFilter.SetMatrix4("u_InverseProjection", inv_projection);
				MainTemporalFilter.SetMatrix4("u_PrevInverseProjection", glm::inverse(PreviousProjection));
				MainTemporalFilter.SetMatrix4("u_PrevInverseView", glm::inverse(PreviousView));

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture());

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture());

				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(1));

				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(1));

				glActiveTexture(GL_TEXTURE8);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

				glActiveTexture(GL_TEXTURE10);
				glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(3));
				glActiveTexture(GL_TEXTURE11);
				glBindTexture(GL_TEXTURE_2D, PrevDiffuseTemporalFBO.GetTexture(3));

				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();

				DiffuseTemporalFBO.Unbind();

				// Do a variance estimation pass

				VarianceFBO.Bind();
				VarianceEstimator.Use();
				VarianceEstimator.SetInteger("u_InputTexture", 0);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, DiffuseTemporalFBO.GetTexture());
				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();
				VarianceFBO.Unbind();

				//
				/// Do 5 atrous spatial filter pass with varying step sizes
				//

				int StepSizes[5] = { 12, 8, 4, 2, 1 };

				for (int i = 0; i < 5; i++)
				{
					GLuint AOTexture = 0;

					// 1 2 1 2 1
					auto& CurrentDenoiseFBO = (i % 2 == 0) ? DiffuseDenoiseFBO : DiffuseDenoisedFBO2;
					auto& PrevDenoiseFBO = (i == 0) ? DiffuseTemporalFBO :
						(i % 2 == 0) ? DiffuseDenoisedFBO2 : DiffuseDenoiseFBO;

					CurrentDenoiseFBO.Bind();
					AtrousSpatialFilter.Use();
					AtrousSpatialFilter.SetInteger("u_InputTexture", 0);
					AtrousSpatialFilter.SetInteger("u_PositionTexture", 1);
					AtrousSpatialFilter.SetInteger("u_NormalTexture", 2);
					AtrousSpatialFilter.SetInteger("u_BlockIDTexture", 3);
					AtrousSpatialFilter.SetInteger("u_VarianceTexture", 4);
					AtrousSpatialFilter.SetInteger("u_InputTexture2", 5);
					AtrousSpatialFilter.SetInteger("u_AO", 6);
					AtrousSpatialFilter.SetInteger("u_Step", StepSizes[i]);
					AtrousSpatialFilter.SetVector2f("u_Dimensions", glm::vec2(DiffuseTemporalFBO.GetWidth(), DiffuseTemporalFBO.GetHeight()));
					AtrousSpatialFilter.SetMatrix4("u_VertInverseView", inv_view);
					AtrousSpatialFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
					AtrousSpatialFilter.SetMatrix4("u_InverseView", inv_view);
					AtrousSpatialFilter.SetMatrix4("u_InverseProjection", inv_projection);
					AtrousSpatialFilter.SetBool("u_ShouldDetailWeight", !(i >= 3));

					// ao texture

					if (i == 0)
					{
						AOTexture = DiffuseTemporalFBO.GetTexture(3);
					}

					else {

						if (i % 2 == 0)
						{
							AOTexture = DiffuseDenoisedFBO2.GetTexture(3);
						}

						else {
							AOTexture = DiffuseDenoiseFBO.GetTexture(3);
						}
					}

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, PrevDenoiseFBO.GetTexture());

					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

					glActiveTexture(GL_TEXTURE2);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

					glActiveTexture(GL_TEXTURE3);
					glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

					glActiveTexture(GL_TEXTURE4);
					glBindTexture(GL_TEXTURE_2D, VarianceFBO.GetTexture());

					glActiveTexture(GL_TEXTURE5);
					glBindTexture(GL_TEXTURE_2D, PrevDenoiseFBO.GetTexture(1));

					glActiveTexture(GL_TEXTURE6);
					glBindTexture(GL_TEXTURE_2D, AOTexture);

					VAO.Bind();
					glDrawArrays(GL_TRIANGLES, 0, 6);
					VAO.Unbind();
				}

				glUseProgram(0);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}


		// ---- SHADOW TRACE ----

		GLClasses::Framebuffer& ShadowFBO = ShadowRawTrace;

		{
			ShadowFBO.Bind();
			ShadowTraceShader.Use();

			ShadowTraceShader.SetInteger("u_PositionTexture", 0);
			ShadowTraceShader.SetInteger("u_VoxelData", 1);
			ShadowTraceShader.SetInteger("u_AlbedoTextures", 2);
			ShadowTraceShader.SetInteger("u_NormalTexture", 3);
			ShadowTraceShader.SetInteger("u_DistanceFieldTexture", 5);
			ShadowTraceShader.SetInteger("u_BlueNoiseTexture", 6);

			ShadowTraceShader.SetVector3f("u_LightDirection", StrongerLightDirection);
			ShadowTraceShader.SetVector3f("u_PlayerPosition", MainCamera.GetPosition());
			ShadowTraceShader.SetBool("u_DoFullTrace", true);
			ShadowTraceShader.SetMatrix4("u_ShadowProjection", ShadowProjection);
			ShadowTraceShader.SetMatrix4("u_ShadowView", ShadowView);

			ShadowTraceShader.SetMatrix4("u_VertInverseView", inv_view);
			ShadowTraceShader.SetMatrix4("u_VertInverseProjection", inv_projection);
			ShadowTraceShader.SetMatrix4("u_InverseView", inv_view);
			ShadowTraceShader.SetMatrix4("u_InverseProjection", inv_projection);
			ShadowTraceShader.SetFloat("u_Time", glfwGetTime());
			ShadowTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());

			ShadowTraceShader.SetBool("u_ContactHardeningShadows", SoftShadows);
			ShadowTraceShader.SetBool("u_ShouldAlphaTest", ShouldAlphaTestShadows);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, world->m_DataTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetTextureArray());

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_3D, world->m_DistanceFieldTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

			BlockDataStorageBuffer.Bind(0);

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			ShadowFBO.Unbind();
			ShadowProjection = CurrentProjection;
			ShadowView = CurrentView;
		}

		if (SoftShadows)
		{
			ShadowTemporalFBO.Bind();

			MainTemporalFilter.Use();
			MainTemporalFilter.SetInteger("u_CurrentColorTexture", 0);
			MainTemporalFilter.SetInteger("u_CurrentPositionTexture", 1);
			MainTemporalFilter.SetInteger("u_PreviousColorTexture", 2);
			MainTemporalFilter.SetInteger("u_PreviousFramePositionTexture", 3);
			MainTemporalFilter.SetInteger("u_NormalTexture", 8);

			MainTemporalFilter.SetMatrix4("u_Projection", CurrentProjection);
			MainTemporalFilter.SetMatrix4("u_View", CurrentView);
			MainTemporalFilter.SetMatrix4("u_PrevProjection", PreviousProjection);
			MainTemporalFilter.SetMatrix4("u_PrevView", PreviousView);
			MainTemporalFilter.SetFloat("u_ClampBias", 0.001f);
			MainTemporalFilter.SetBool("u_DiffuseTemporal", false);
			MainTemporalFilter.SetBool("u_ShadowTemporal", true);

			MainTemporalFilter.SetFloat("u_MinimumMix", 0.0f);
			MainTemporalFilter.SetFloat("u_MaximumMix", ModifiedWorld ? 0.1f : 0.95f);
			MainTemporalFilter.SetInteger("u_TemporalQuality", 1);
			MainTemporalFilter.SetBool("u_ReflectionTemporal", false);

			MainTemporalFilter.SetMatrix4("u_VertInverseView", inv_view);
			MainTemporalFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
			MainTemporalFilter.SetMatrix4("u_InverseView", inv_view);
			MainTemporalFilter.SetMatrix4("u_InverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ShadowRawTrace.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, PrevShadowTemporalFBO.GetTexture());

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

			glActiveTexture(GL_TEXTURE8);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			ShadowTemporalFBO.Unbind();
		}

		// Final shadow noise cleanup
		if (SoftShadows)
		{
			ShadowFiltered.Bind();
			ShadowFilter.Use();
			ShadowFilter.SetInteger("u_InputTexture", 0);
			ShadowFilter.SetInteger("u_PositionTexture", 1);
			ShadowFilter.SetInteger("u_NormalTexture", 2);
			ShadowFilter.SetVector2f("u_Dimensions", glm::vec2(DiffuseDenoisedFBO2.GetWidth(), DiffuseDenoisedFBO2.GetHeight()));
			ShadowFilter.SetMatrix4("u_VertInverseView", inv_view);
			ShadowFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
			ShadowFilter.SetMatrix4("u_InverseView", inv_view);
			ShadowFilter.SetMatrix4("u_InverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ShadowTemporalFBO.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();
		}

		glUseProgram(0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// ---- REFLECTION TRACE ----


		{
			ReflectionTraceFBO.Bind();
			ReflectionTraceShader.Use();

			ReflectionTraceShader.SetInteger("u_PositionTexture", 0);
			ReflectionTraceShader.SetInteger("u_BlockIDTex", 1);
			ReflectionTraceShader.SetInteger("u_InitialTraceNormalTexture", 2);
			ReflectionTraceShader.SetInteger("u_BlockNormalTextures", 4);
			ReflectionTraceShader.SetInteger("u_BlockAlbedoTextures", 5);
			ReflectionTraceShader.SetInteger("u_BlockPBRTextures", 6);
			ReflectionTraceShader.SetInteger("u_Skymap", 7);
			ReflectionTraceShader.SetInteger("u_VoxelData", 8);
			ReflectionTraceShader.SetInteger("u_BlueNoiseTexture", 9);
			ReflectionTraceShader.SetInteger("u_DistanceFieldTexture", 10);
			ReflectionTraceShader.SetInteger("u_BlockEmissiveTextures", 11);
			ReflectionTraceShader.SetInteger("u_DiffuseSH", 14);
			ReflectionTraceShader.SetInteger("u_DiffuseCoCg", 15);
			ReflectionTraceShader.SetInteger("u_ShadowTrace", 16);

			ReflectionTraceShader.SetFloat("u_ReflectionTraceRes", ReflectionTraceResolution);
			ReflectionTraceShader.SetVector3f("u_SunDirection", SunDirection);
			ReflectionTraceShader.SetVector3f("u_StrongerLightDirection", StrongerLightDirection);
			ReflectionTraceShader.SetVector2f("u_Dimensions", glm::vec2(ReflectionTraceFBO.GetWidth(), ReflectionTraceFBO.GetHeight()));
			ReflectionTraceShader.SetFloat("u_Time", glfwGetTime());
			ReflectionTraceShader.SetVector3f("u_ViewerPosition", MainCamera.GetPosition());
			ReflectionTraceShader.SetBool("u_RoughReflections", RoughReflections);
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[0]", VoxelRT::BlockDatabase::GetBlockID("Grass"));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[1]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[2]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[3]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[4]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[5]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[6]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[7]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[8]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));
			ReflectionTraceShader.SetInteger("u_GrassBlockProps[9]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));
			ReflectionTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
			ReflectionTraceShader.SetInteger("u_PlayerSprite", 12);
			ReflectionTraceShader.SetInteger("u_SPP", ReflectionSPP);
			ReflectionTraceShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());

			ReflectionTraceShader.SetMatrix4("u_VertInverseView", inv_view);
			ReflectionTraceShader.SetMatrix4("u_VertInverseProjection", inv_projection);
			ReflectionTraceShader.SetMatrix4("u_InverseView", inv_view);
			ReflectionTraceShader.SetMatrix4("u_InverseProjection", inv_projection);
			ReflectionTraceShader.SetMatrix4("u_View", MainCamera.GetViewMatrix());
			ReflectionTraceShader.SetMatrix4("u_Projection", MainCamera.GetProjectionMatrix());
			ReflectionTraceShader.SetBool("u_ReprojectToScreenSpace", ReprojectReflectionsToScreenSpace);

			//ReflectionTraceShader.BindUBOToBindingPoint("UBO_BlockData", 0);
			BlockDataStorageBuffer.Bind(0);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetNormalTextureArray());

			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetTextureArray());

			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetPBRTextureArray());

			glActiveTexture(GL_TEXTURE7);
			glBindTexture(GL_TEXTURE_CUBE_MAP, Skymap.GetTexture());

			glActiveTexture(GL_TEXTURE8);
			glBindTexture(GL_TEXTURE_3D, world->m_DataTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE9);
			glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE10);
			glBindTexture(GL_TEXTURE_3D, world->m_DistanceFieldTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE11);
			glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetEmissiveTextureArray());

			glActiveTexture(GL_TEXTURE12);
			glBindTexture(GL_TEXTURE_2D, PlayerSprite.GetTextureID());

			glActiveTexture(GL_TEXTURE14);
			glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture(0));

			glActiveTexture(GL_TEXTURE15);
			glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture(1));

			glActiveTexture(GL_TEXTURE16);
			glBindTexture(GL_TEXTURE_2D, SoftShadows ? ShadowFiltered.GetTexture() : ShadowRawTrace.GetTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			ReflectionTraceFBO.Unbind();
		}

		// Temporally filter it
		if (RoughReflections)
		{
			ReflectionTemporalFBO.Bind();
			SpecularTemporalFilter.Use();

			SpecularTemporalFilter.SetInteger("u_CurrentColorTexture", 0);
			SpecularTemporalFilter.SetInteger("u_CurrentPositionTexture", 1);
			SpecularTemporalFilter.SetInteger("u_PreviousColorTexture", 2);
			SpecularTemporalFilter.SetInteger("u_PreviousFramePositionTexture", 3);
			SpecularTemporalFilter.SetInteger("u_PBRTex", 4);

			SpecularTemporalFilter.SetInteger("u_CurrentCoCg", 5);
			SpecularTemporalFilter.SetInteger("u_PrevCoCg", 6);

			SpecularTemporalFilter.SetInteger("u_SpecularHitDist", 7);
			SpecularTemporalFilter.SetInteger("u_PrevSpecularHitDist", 8);
			SpecularTemporalFilter.SetInteger("u_NormalTexture", 9);


			SpecularTemporalFilter.SetMatrix4("u_Projection", CurrentProjection);
			SpecularTemporalFilter.SetMatrix4("u_View", CurrentView);
			SpecularTemporalFilter.SetMatrix4("u_PrevProjection", PreviousProjection);
			SpecularTemporalFilter.SetMatrix4("u_PrevView", PreviousView);

			SpecularTemporalFilter.SetFloat("u_MinimumMix", 0.0f); // Brutal temporal filtering
			SpecularTemporalFilter.SetFloat("u_MaximumMix", 0.95f);
			SpecularTemporalFilter.SetInteger("u_TemporalQuality", 1);
			SpecularTemporalFilter.SetBool("u_ReflectionTemporal", true);

			SpecularTemporalFilter.SetVector3f("u_PrevCameraPos", PreviousPosition);
			SpecularTemporalFilter.SetVector3f("u_CurrentCameraPos", MainCamera.GetPosition());

			SpecularTemporalFilter.SetMatrix4("u_VertInverseView", inv_view);
			SpecularTemporalFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
			SpecularTemporalFilter.SetMatrix4("u_InverseView", inv_view);
			SpecularTemporalFilter.SetMatrix4("u_InverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ReflectionTraceFBO.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, PrevReflectionTemporalFBO.GetTexture());

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetPBRTexture());


			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D, ReflectionTraceFBO.GetTexture(1));
			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_2D, PrevReflectionTemporalFBO.GetTexture(1));

			glActiveTexture(GL_TEXTURE7);
			glBindTexture(GL_TEXTURE_2D, ReflectionTraceFBO.GetTexture(2));
			glActiveTexture(GL_TEXTURE8);
			glBindTexture(GL_TEXTURE_2D, PrevReflectionTraceFBO.GetTexture(2));

			glActiveTexture(GL_TEXTURE9);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			ReflectionTemporalFBO.Unbind();
		}

		// Denoise reflection trace
		if (DenoiseReflections && RoughReflections)
		{
			// x pass
				
			{
				ReflectionDenoised_1.Bind();
				ReflectionDenoiser.Use();

				ReflectionDenoiser.SetInteger("u_InputTexture", 0);
				ReflectionDenoiser.SetInteger("u_PositionTexture", 1);
				ReflectionDenoiser.SetInteger("u_NormalTexture", 2);
				ReflectionDenoiser.SetInteger("u_PBRTex", 3);
				ReflectionDenoiser.SetInteger("u_NormalMappedTexture", 4);
				ReflectionDenoiser.SetInteger("u_InputCoCgTexture", 5);
				ReflectionDenoiser.SetInteger("u_Step", 1);
				ReflectionDenoiser.SetBool("u_Dir", true);
				ReflectionDenoiser.SetVector2f("u_Dimensions", glm::vec2(DiffuseTemporalFBO.GetWidth(), DiffuseTemporalFBO.GetHeight()));
				ReflectionDenoiser.SetMatrix4("u_VertInverseView", inv_view);
				ReflectionDenoiser.SetMatrix4("u_VertInverseProjection", inv_projection);
				ReflectionDenoiser.SetMatrix4("u_InverseView", inv_view);
				ReflectionDenoiser.SetMatrix4("u_InverseProjection", inv_projection);
				ReflectionDenoiser.SetMatrix4("u_PrevProjection", PreviousProjection);
				ReflectionDenoiser.SetMatrix4("u_PrevView", PreviousView);

				ReflectionDenoiser.SetInteger("u_BlockIDTex", 6);
				ReflectionDenoiser.SetInteger("u_BlockPBRTexArray", 7);

				BlockDataStorageBuffer.Bind(0);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, ReflectionTemporalFBO.GetTexture());

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetPBRTexture());

				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetNormalTexture());

				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, ReflectionTemporalFBO.GetTexture(1));

				glActiveTexture(GL_TEXTURE6);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

				glActiveTexture(GL_TEXTURE7);
				glBindTexture(GL_TEXTURE_2D_ARRAY, BlockDatabase::GetPBRTextureArray());

				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();
			}

			// y pass 

			{
				ReflectionDenoised_2.Bind();
				ReflectionDenoiser.Use();

				ReflectionDenoiser.SetInteger("u_InputTexture", 0);
				ReflectionDenoiser.SetInteger("u_PositionTexture", 1);
				ReflectionDenoiser.SetInteger("u_NormalTexture", 2);
				ReflectionDenoiser.SetInteger("u_InputCoCgTexture", 5);
				ReflectionDenoiser.SetInteger("u_BlockIDTex", 6);
				ReflectionDenoiser.SetInteger("u_BlockPBRTexArray", 7);
				ReflectionDenoiser.SetInteger("u_Step", 1);
				ReflectionDenoiser.SetBool("u_Dir", false);
				ReflectionDenoiser.SetVector2f("u_Dimensions", glm::vec2(DiffuseDenoisedFBO2.GetWidth(), DiffuseDenoisedFBO2.GetHeight()));
				ReflectionDenoiser.SetMatrix4("u_VertInverseView", inv_view);
				ReflectionDenoiser.SetMatrix4("u_VertInverseProjection", inv_projection);
				ReflectionDenoiser.SetMatrix4("u_InverseView", inv_view);
				ReflectionDenoiser.SetMatrix4("u_InverseProjection", inv_projection);
				ReflectionDenoiser.SetMatrix4("u_PrevProjection", PreviousProjection);
				ReflectionDenoiser.SetMatrix4("u_PrevView", PreviousView);

				BlockDataStorageBuffer.Bind(0);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, ReflectionDenoised_1.GetTexture());

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, ReflectionDenoised_1.GetTexture(1));

				glActiveTexture(GL_TEXTURE6);
				glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

				glActiveTexture(GL_TEXTURE7);
				glBindTexture(GL_TEXTURE_2D_ARRAY, BlockDatabase::GetPBRTextureArray());
				
				VAO.Bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
				VAO.Unbind();
			}
		}

		// ---- RT AO ----

		if (RTAO)
		{
			RTAO_FBO.Bind();
			RTAOShader.Use();

			RTAOShader.SetInteger("u_PositionTexture", 0);
			RTAOShader.SetInteger("u_VoxelData", 1);
			RTAOShader.SetInteger("u_NormalTexture", 2);
			RTAOShader.SetInteger("u_BlockAlbedoTextures", 3);
			RTAOShader.SetInteger("u_BlockNormalTextures", 4);
			RTAOShader.SetInteger("u_BlockIDTexture", 5);
			RTAOShader.SetFloat("u_Time", glfwGetTime());
			RTAOShader.SetMatrix4("u_VertInverseView", inv_view);
			RTAOShader.SetMatrix4("u_VertInverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, world->m_DataTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D_ARRAY, BlockDatabase::GetTextureArray());

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D_ARRAY, BlockDatabase::GetNormalTextureArray());

			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

			BlockDataStorageBuffer.Bind(0);

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			RTAO_FBO.Unbind();

			// Temporally filter it
			RTAOTemporalFBO.Bind();
			MainTemporalFilter.Use();

			MainTemporalFilter.SetInteger("u_CurrentColorTexture", 0);
			MainTemporalFilter.SetInteger("u_CurrentPositionTexture", 1);
			MainTemporalFilter.SetInteger("u_PreviousColorTexture", 2);
			MainTemporalFilter.SetInteger("u_PreviousFramePositionTexture", 3);

			MainTemporalFilter.SetMatrix4("u_Projection", CurrentProjection);
			MainTemporalFilter.SetMatrix4("u_View", CurrentView);
			MainTemporalFilter.SetMatrix4("u_PrevProjection", PreviousProjection);
			MainTemporalFilter.SetMatrix4("u_PrevView", PreviousView);
			MainTemporalFilter.SetBool("u_DiffuseTemporal", false);
			MainTemporalFilter.SetBool("u_ShadowTemporal", false);

			MainTemporalFilter.SetFloat("u_MinimumMix", 0.25f);
			MainTemporalFilter.SetFloat("u_MaximumMix", 0.975f);
			MainTemporalFilter.SetInteger("u_TemporalQuality", 0);
			MainTemporalFilter.SetBool("u_ReflectionTemporal", false);

			MainTemporalFilter.SetMatrix4("u_VertInverseView", inv_view);
			MainTemporalFilter.SetMatrix4("u_VertInverseProjection", inv_projection);
			MainTemporalFilter.SetMatrix4("u_InverseView", inv_view);
			MainTemporalFilter.SetMatrix4("u_InverseProjection", inv_projection);
			MainTemporalFilter.SetFloat("u_ClampBias", 0.02f);
			MainTemporalFilter.SetInteger("u_NormalTexture", 8);

			glActiveTexture(GL_TEXTURE8);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, RTAO_FBO.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, PrevRTAOTemporalFBO.GetTexture());

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			RTAOTemporalFBO.Unbind();
		}

		// ---- RENDER CLOUDS ----
		GLuint CloudData = 0;
		if (CloudsEnabled)
		{
			CloudData = Clouds::CloudRenderer::Update(MainCamera, PreviousProjection,
				PreviousView, CurrentPosition,
				PreviousPosition, VAO, StrongerLightDirection, BluenoiseTexture.GetTextureID(),
				PADDED_WIDTH, PADDED_HEIGHT, app.GetCurrentFrame(), Skymap.GetTexture(), InitialTraceFBO->GetTexture(0), PreviousPosition, InitialTraceFBOPrev->GetTexture(0));

			Clouds::CloudRenderer::SetChecker(CheckerboardClouds);
			Clouds::CloudRenderer::SetCoverage(CloudCoverage);
			Clouds::CloudRenderer::SetBayer(CloudBayer);
			Clouds::CloudRenderer::SetDetailContribution(CloudDetailContribution);
			Clouds::CloudRenderer::SetQuality(CloudHighQuality);
			Clouds::CloudRenderer::SetResolution(CloudResolution);
		}

		// ---- COLOR PASS ----

		ReflectionProjection = PreviousProjection;
		ReflectionView = PreviousView;

		ColoredFBO.Bind();

		ColorShader.Use();
		ColorShader.SetInteger("u_DiffuseTexture", 0);
		ColorShader.SetInteger("u_NormalTexture", 1);
		ColorShader.SetInteger("u_InitialTracePositionTexture", 2);
		ColorShader.SetInteger("u_BlockIDTexture", 3);
		ColorShader.SetInteger("u_BlockAlbedoTextures", 4);
		ColorShader.SetInteger("u_BlockNormalTextures", 5);
		ColorShader.SetInteger("u_BlockPBRTextures", 6);
		ColorShader.SetInteger("u_Skybox", 7);
		ColorShader.SetInteger("u_ShadowTexture", 8);
		ColorShader.SetInteger("u_BlueNoiseTextures", 9);
		ColorShader.SetInteger("u_BlockEmissiveTextures", 11);
		ColorShader.SetInteger("u_CloudData", 12);
		ColorShader.SetInteger("u_DiffuseSHData1", 14);
		ColorShader.SetInteger("u_DiffuseSHData2", 15);

		/*
		 u_ReflectionSHData;
		 u_ReflectionCoCgData;
		*/

		ColorShader.SetInteger("u_ReflectionSHData", 16);
		ColorShader.SetInteger("u_ReflectionCoCgData", 17);
		ColorShader.SetInteger("u_VXAO", 18);
		ColorShader.SetInteger("u_HighResBL", 19);
		ColorShader.SetInteger("u_ContactHardeningShadows", true);
		ColorShader.SetMatrix4("u_InverseView", inv_view);
		ColorShader.SetMatrix4("u_InverseProjection", inv_projection);
		ColorShader.SetMatrix4("u_View", MainCamera.GetViewMatrix());
		ColorShader.SetMatrix4("u_Projection", MainCamera.GetProjectionMatrix());
		ColorShader.SetMatrix4("u_ShadowProjection", ShadowProjection);
		ColorShader.SetMatrix4("u_ShadowView", ShadowView);
		ColorShader.SetMatrix4("u_ReflectionProjection", ReflectionProjection);
		ColorShader.SetMatrix4("u_ReflectionView", ReflectionView);
		ColorShader.SetVector2f("u_InitialTraceResolution", glm::vec2(floor(PADDED_WIDTH * InitialTraceResolution), floor(PADDED_HEIGHT * InitialTraceResolution)));
		ColorShader.SetVector3f("u_SunDirection", SunDirection);
		ColorShader.SetVector3f("u_MoonDirection", MoonDirection);
		ColorShader.SetVector3f("u_StrongerLightDirection", StrongerLightDirection);
		ColorShader.SetVector3f("u_ViewerPosition", MainCamera.GetPosition());
		ColorShader.SetFloat("u_Time", glfwGetTime());
		ColorShader.SetFloat("u_GrassblockAlbedoID", BlockDatabase::GetBlockTexture("Grass", BlockDatabase::BlockFaceType::Front));
		ColorShader.SetFloat("u_CloudBoxSize", Clouds::CloudRenderer::GetBoxSize());
		ColorShader.SetBool("u_CloudsEnabled", CloudsEnabled);
		ColorShader.SetBool("u_POM", POM);
		ColorShader.SetBool("u_HighQualityPOM", HighQualityPOM);
		ColorShader.SetBool("u_RTAO", RTAO);
		ColorShader.SetBool("u_AmplifyNormalMap", AmplifyNormalMap);
		ColorShader.SetBool("u_DoVXAO", VXAO);
		ColorShader.SetBool("u_SVGFEnabled", USE_SVGF);
		ColorShader.SetBool("u_ShouldDitherUpscale", DITHER_SPATIAL_UPSCALE);
		ColorShader.SetVector2f("u_Dimensions", glm::vec2(PADDED_WIDTH, PADDED_HEIGHT));
		ColorShader.SetMatrix4("u_InverseView", inv_view);
		ColorShader.SetMatrix4("u_InverseProjection", inv_projection);




		ColorShader.SetInteger("u_GrassBlockProps[0]", VoxelRT::BlockDatabase::GetBlockID("Grass"));
		ColorShader.SetInteger("u_GrassBlockProps[1]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
		ColorShader.SetInteger("u_GrassBlockProps[2]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
		ColorShader.SetInteger("u_GrassBlockProps[3]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Top));
		ColorShader.SetInteger("u_GrassBlockProps[4]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
		ColorShader.SetInteger("u_GrassBlockProps[5]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
		ColorShader.SetInteger("u_GrassBlockProps[6]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Front));
		ColorShader.SetInteger("u_GrassBlockProps[7]", VoxelRT::BlockDatabase::GetBlockTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));
		ColorShader.SetInteger("u_GrassBlockProps[8]", VoxelRT::BlockDatabase::GetBlockNormalTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));
		ColorShader.SetInteger("u_GrassBlockProps[9]", VoxelRT::BlockDatabase::GetBlockPBRTexture("Grass", VoxelRT::BlockDatabase::BlockFaceType::Bottom));



		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetTextureArray());

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetNormalTextureArray());

		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetPBRTextureArray());

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_CUBE_MAP, Skymap.GetTexture());

		glActiveTexture(GL_TEXTURE8);
		glBindTexture(GL_TEXTURE_2D, SoftShadows ? ShadowFiltered.GetTexture() : ShadowRawTrace.GetTexture());

		glActiveTexture(GL_TEXTURE9);
		glBindTexture(GL_TEXTURE_2D_ARRAY, BlueNoise.GetTextureArray());

		//glActiveTexture(GL_TEXTURE10);
		//glBindTexture(GL_TEXTURE_2D, RoughReflections ? (DenoiseReflections ? ReflectionDenoised_2.GetTexture() : ReflectionTemporalFBO.GetTexture()) : ReflectionCheckerReconstructed.GetTexture());

		glActiveTexture(GL_TEXTURE11);
		glBindTexture(GL_TEXTURE_2D_ARRAY, VoxelRT::BlockDatabase::GetEmissiveTextureArray());

		glActiveTexture(GL_TEXTURE12);
		glBindTexture(GL_TEXTURE_2D, CloudData);

		glActiveTexture(GL_TEXTURE14);
		glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture(0));
		//glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(0));

		glActiveTexture(GL_TEXTURE15);
		glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture(1));
		//glBindTexture(GL_TEXTURE_2D, DiffuseTraceFBO.GetTexture(1));

		glActiveTexture(GL_TEXTURE16);
		glBindTexture(GL_TEXTURE_2D, RoughReflections ? (DenoiseReflections ? ReflectionDenoised_2.GetTexture() : ReflectionTemporalFBO.GetTexture()) : ReflectionTraceFBO.GetTexture());
		glActiveTexture(GL_TEXTURE17);
		glBindTexture(GL_TEXTURE_2D, RoughReflections ? (DenoiseReflections ? ReflectionDenoised_2.GetTexture(1) : ReflectionTemporalFBO.GetTexture(1)) : ReflectionTraceFBO.GetTexture(1));

		glActiveTexture(GL_TEXTURE18);
		glBindTexture(GL_TEXTURE_2D, DiffuseDenoiseFBO.GetTexture(3));

		glActiveTexture(GL_TEXTURE19);
		glBindTexture(GL_TEXTURE_2D, BluenoiseHighResTexture.GetTextureID());

		BlockDataStorageBuffer.Bind(0);

		VAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		VAO.Unbind();

		ColoredFBO.Unbind();

		// ----- TAA ----- //

		TAAFBO.Bind();

		TemporalAAShader.Use();
		TemporalAAShader.SetInteger("u_CurrentColorTexture", 0);
		TemporalAAShader.SetInteger("u_PositionTexture", 1);
		TemporalAAShader.SetInteger("u_PreviousColorTexture", 2);
		TemporalAAShader.SetInteger("u_PreviousPositionTexture", 3);
		TemporalAAShader.SetBool("u_Enabled", TAA);
		TemporalAAShader.SetBool("u_BlockModified", ModifiedWorld);

		TemporalAAShader.SetMatrix4("u_PrevProjection", PreviousProjection);
		TemporalAAShader.SetMatrix4("u_PrevView", PreviousView);
		TemporalAAShader.SetMatrix4("u_VertInverseView", inv_view);
		TemporalAAShader.SetMatrix4("u_VertInverseProjection", inv_projection);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetColorTexture());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, PrevTAAFBO.GetTexture());

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBOPrev->GetTexture(0));

		VAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		VAO.Unbind();

		// ---- SSAO ---- //

		if (SSAO)
		{
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_CULL_FACE);
			glDisable(GL_BLEND);

			SSAOShader.Use();
			SSAOFBO.Bind();

			SSAOShader.SetInteger("u_PositionTexture", 0);
			SSAOShader.SetInteger("u_NormalTexture", 1);
			SSAOShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
			SSAOShader.SetInteger("SAMPLE_SIZE", 20);
			SSAOShader.SetMatrix4("u_ViewMatrix", MainCamera.GetViewMatrix());
			SSAOShader.SetMatrix4("u_ProjectionMatrix", MainCamera.GetProjectionMatrix());
			SSAOShader.SetVector2f("u_Dimensions", glm::vec2(SSAOFBO.GetWidth(), SSAOFBO.GetHeight()));
			SSAOShader.SetFloat("u_Time", glfwGetTime());
			SSAOShader.SetMatrix4("u_VertInverseView", inv_view);
			SSAOShader.SetMatrix4("u_VertInverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetNormalTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();
			SSAOFBO.Unbind();
			glUseProgram(0);

			SSAOBlurred.Bind();
			SSAO_Blur.Use();
			SSAO_Blur.SetInteger("u_Texture", 0);
			SSAO_Blur.SetVector2f("u_SketchSize", glm::vec2(SSAOFBO.GetWidth(), SSAOFBO.GetHeight()));

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, SSAOFBO.GetTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			SSAOBlurred.Unbind();

			glUseProgram(0);
		}

		// ---- Bloom ----

		if (Bloom)
		{
			BloomRenderer::RenderBloom(BloomFBO, ColoredFBO.GetColorTexture(), ColoredFBO.GetPBRTexture(), Bloom_HQ);
		}

		// ---- Auto Exposure ----


		// This was to test auto exposure, it doesnt work very well




		float ComputedExposure = 3.0f;

		if (AutoExposure)
		{
#define SAMPLE_COUNT 9
			int AutoExposureWidth = PADDED_WIDTH;
			int AutoExposureHeight = PADDED_HEIGHT;
			float AverageBrightness;

			int Pixels[SAMPLE_COUNT][2] =
			{
				static_cast<int>(AutoExposureWidth * 0.50), static_cast<int>(AutoExposureHeight * 0.50),
				static_cast<int>(AutoExposureWidth * 0.25), static_cast<int>(AutoExposureHeight * 0.50),
				static_cast<int>(AutoExposureWidth * 0.75), static_cast<int>(AutoExposureHeight * 0.50),
				static_cast<int>(AutoExposureWidth * 0.50), static_cast<int>(AutoExposureHeight * 0.25),
				static_cast<int>(AutoExposureWidth * 0.50), static_cast<int>(AutoExposureHeight * 0.75),
				static_cast<int>(AutoExposureWidth * 0.25), static_cast<int>(AutoExposureHeight * 0.25),
				static_cast<int>(AutoExposureWidth * 0.25), static_cast<int>(AutoExposureHeight * 0.75),
				static_cast<int>(AutoExposureWidth * 0.75), static_cast<int>(AutoExposureHeight * 0.25),
				static_cast<int>(AutoExposureWidth * 0.75), static_cast<int>(AutoExposureHeight * 0.75)
			};

			glm::vec3 AveragedSamples;
			unsigned char Samples[SAMPLE_COUNT][4];

			for (int i = 0; i < SAMPLE_COUNT; i++)
			{
				glReadPixels(Pixels[i][0], Pixels[i][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &Samples[i][0]);
				AveragedSamples += (glm::vec3(Samples[i][0], Samples[i][1], Samples[i][2]) / 255.0f) / float(SAMPLE_COUNT);
			}

			AverageBrightness = glm::max(glm::max(AveragedSamples.x, AveragedSamples.y), AveragedSamples.z);
			CameraExposure = 0.5 / AverageBrightness;
			CameraExposure = PrevCameraExposure + (CameraExposure - PrevCameraExposure) * 0.02;
			PrevCameraExposure = CameraExposure;
			ComputedExposure = CameraExposure * 3.0f;
		}




		// ---- Volumetric Scattering ----

		if (GodRays)
		{
			VolumetricFBO.Bind();
			VolumetricScattering.Use();

			VolumetricScattering.SetInteger("u_PositionTexture", 0);
			VolumetricScattering.SetInteger("u_BlueNoiseTexture", 1);
			VolumetricScattering.SetVector3f("u_StrongerLightDirection", StrongerLightDirection);
			VolumetricScattering.SetVector2f("u_Dimensions", glm::vec2(VolumetricFBO.GetWidth(), VolumetricFBO.GetHeight()));
			VolumetricScattering.SetMatrix4("u_ProjectionMatrix", MainCamera.GetProjectionMatrix());
			VolumetricScattering.SetMatrix4("u_ViewMatrix", MainCamera.GetViewMatrix());

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			VolumetricFBO.Unbind();

			// Horizontal Blur 

			BlurredVolumetricFBO.Bind();
			BilateralBlur.Use();

			BilateralBlur.SetFloat("u_Sharpness", 0.5f);
			BilateralBlur.SetVector2f("u_InvResolutionDirection", glm::vec2(1.0f / BlurredVolumetricFBO.GetWidth(), 0.0f));
			BilateralBlur.SetInteger("u_ColorTexture", 0);
			BilateralBlur.SetInteger("u_PositionTexture", 1);
			BilateralBlur.SetMatrix4("u_VertInverseView", inv_view);
			BilateralBlur.SetMatrix4("u_VertInverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, VolumetricFBO.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			BlurredVolumetricFBO.Unbind();

			// Vertical blur

			VolumetricFBO.Bind();

			BilateralBlur.Use();

			BilateralBlur.SetFloat("u_Sharpness", 0.5f);
			BilateralBlur.SetVector2f("u_InvResolutionDirection", glm::vec2(0.0f, 1.0f / BlurredVolumetricFBO.GetHeight()));
			BilateralBlur.SetInteger("u_ColorTexture", 0);
			BilateralBlur.SetInteger("u_PositionTexture", 1);
			BilateralBlur.SetMatrix4("u_VertInverseView", inv_view);
			BilateralBlur.SetMatrix4("u_VertInverseProjection", inv_projection);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, BlurredVolumetricFBO.GetTexture());

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			VolumetricFBO.Unbind();
		}

		// ACTUAL WORLD SPACE VOLUMETRICS //

		if (PointVolumetricsToggled)
		{
			PointVolumetrics.Use();
			VolumetricsCompute.Bind();

			PointVolumetrics.SetMatrix4("u_ProjectionMatrix", MainCamera.GetProjectionMatrix());
			PointVolumetrics.SetMatrix4("u_ViewMatrix", MainCamera.GetViewMatrix());
			PointVolumetrics.SetMatrix4("u_InverseView", inv_view);
			PointVolumetrics.SetMatrix4("u_InverseProjection", inv_projection);
			PointVolumetrics.SetVector3f("u_ViewerPosition", MainCamera.GetPosition());

			PointVolumetrics.SetInteger("u_ParticipatingMedia", 0);
			PointVolumetrics.SetInteger("u_BlueNoise", 1);
			PointVolumetrics.SetInteger("u_LinearDepthTexture", 2);

			PointVolumetrics.SetVector2f("u_Dimensions", glm::vec2(VolumetricsCompute.GetWidth(), VolumetricsCompute.GetHeight()));

			glBindImageTexture(0, VoxelRT::Volumetrics::GetVolume(), 0, true, 0, GL_READ_ONLY, GL_R16UI);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, VoxelRT::Volumetrics::GetAverageColorSSBO());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			// Blur
			VolumetricsComputeBlurred.Bind();
			Gaussian9TapOptimized.Use();

			Gaussian9TapOptimized.SetInteger("u_Texture", 0);
			
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, VolumetricsCompute.GetTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();

			// Blur 2

			Gaussian5TapOptimized.Use();
			VolumetricsCompute.Bind();

			Gaussian5TapOptimized.SetInteger("u_Texture", 0);
			
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, VolumetricsComputeBlurred.GetTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();
		}

		// ---- POST PROCESSING ----

		PostProcessingFBO.Bind();
		PostProcessingShader.Use();

		PostProcessingShader.SetMatrix4("u_InverseView", inv_view);
		PostProcessingShader.SetMatrix4("u_InverseProjection", inv_projection);
		PostProcessingShader.SetInteger("u_FramebufferTexture", 0);
		PostProcessingShader.SetInteger("u_PositionTexture", 1);
		PostProcessingShader.SetInteger("u_BlueNoise", 2);
		PostProcessingShader.SetInteger("u_SSAOTexture", 3);
		PostProcessingShader.SetInteger("u_VolumetricTexture", 10);
		PostProcessingShader.SetInteger("u_RTAOTexture", 11);
		PostProcessingShader.SetInteger("u_NormalTexture", 12);
		PostProcessingShader.SetInteger("u_PBRTexture", 13);
		PostProcessingShader.SetInteger("u_CloudData", 15);
		PostProcessingShader.SetInteger("u_VolumetricsCompute", 16);
		PostProcessingShader.SetInteger("u_BlockIDTexture", 17);
		PostProcessingShader.SetInteger("u_GodRaysStepCount", GodRaysStepCount);
		PostProcessingShader.SetVector3f("u_SunDirection", SunDirection);
		PostProcessingShader.SetVector3f("u_StrongerLightDirection", StrongerLightDirection);
		PostProcessingShader.SetVector3f("u_ViewerPosition", MainCamera.GetPosition());
		PostProcessingShader.SetVector2f("u_Dimensions", glm::vec2(PostProcessingFBO.GetWidth(), PostProcessingFBO.GetHeight()));
		PostProcessingShader.SetMatrix4("u_ProjectionMatrix", MainCamera.GetProjectionMatrix());
		PostProcessingShader.SetMatrix4("u_ViewMatrix", MainCamera.GetViewMatrix());
		PostProcessingShader.SetBool("u_SunIsStronger", StrongerLightDirection == SunDirection);
		PostProcessingShader.SetBool("u_LensFlare", LensFlare);
		PostProcessingShader.SetBool("u_GodRays", GodRays);
		PostProcessingShader.SetBool("u_SSAO", SSAO);
		PostProcessingShader.SetBool("u_Bloom", Bloom);
		PostProcessingShader.SetBool("u_SSGodRays", FakeGodRays);
		PostProcessingShader.SetBool("u_RTAO", RTAO);
		PostProcessingShader.SetBool("u_ExponentialFog", ExponentialFog);
		PostProcessingShader.SetBool("u_AutoExposure", AutoExposure);
		PostProcessingShader.SetBool("u_PointVolumetricsToggled", PointVolumetricsToggled);
		PostProcessingShader.SetFloat("u_LensFlareIntensity", LensFlareIntensity);
		PostProcessingShader.SetFloat("u_Exposure", ComputedExposure);

		// Set the bloom mips
		PostProcessingShader.SetInteger("u_BloomMips[0]", 5);
		PostProcessingShader.SetInteger("u_BloomMips[1]", 6);
		PostProcessingShader.SetInteger("u_BloomMips[2]", 7);
		PostProcessingShader.SetInteger("u_BloomMips[3]", 8);
		PostProcessingShader.SetInteger("u_ShadowTexture", 9);

		PostProcessingShader.SetMatrix4("u_VertInverseView", inv_view);
		PostProcessingShader.SetMatrix4("u_VertInverseProjection", inv_projection);
		PostProcessingShader.SetMatrix4("u_InverseView", inv_view);
		PostProcessingShader.SetMatrix4("u_InverseProjection", inv_projection);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, TAAFBO.GetTexture());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, BluenoiseTexture.GetTextureID());

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, SSAOBlurred.GetTexture());

		//glActiveTexture(GL_TEXTURE4);
		//glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		// Bloom mips
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, BloomFBO.m_Mips[0]);

		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, BloomFBO.m_Mips[1]);

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, BloomFBO.m_Mips[2]);

		glActiveTexture(GL_TEXTURE8);
		glBindTexture(GL_TEXTURE_2D, BloomFBO.m_Mips[3]);
		//

		glActiveTexture(GL_TEXTURE9);
		glBindTexture(GL_TEXTURE_2D, SoftShadows ? ShadowFiltered.GetTexture() : ShadowRawTrace.GetTexture());

		glActiveTexture(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D, VolumetricFBO.GetTexture());

		glActiveTexture(GL_TEXTURE11);
		glBindTexture(GL_TEXTURE_2D, RTAOTemporalFBO.GetTexture());

		glActiveTexture(GL_TEXTURE12);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

		glActiveTexture(GL_TEXTURE13);
		glBindTexture(GL_TEXTURE_2D, ColoredFBO.GetPBRTexture());

		glActiveTexture(GL_TEXTURE15);
		glBindTexture(GL_TEXTURE_2D, CloudData);

		glActiveTexture(GL_TEXTURE16);
		glBindTexture(GL_TEXTURE_2D, VolumetricsCompute.GetTexture());

		glActiveTexture(GL_TEXTURE17);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

		VAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		VAO.Unbind();

		PostProcessingFBO.Unbind();

		// ---- FINAL ----

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		if (ContrastAdaptiveSharpening) {
			FXAA_Final.Bind();
			glViewport(0, 0, app.GetWidth(), app.GetHeight());
		}

		else {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, app.GetWidth(), app.GetHeight());
		}

		FinalShader.Use();
		FinalShader.SetInteger("u_FramebufferTexture", 0);
		FinalShader.SetInteger("u_PositionTexture", 1);
		FinalShader.SetInteger("u_NormalTexture", 2);
		FinalShader.SetInteger("u_BlockIDTex", 3);
		FinalShader.SetBool("u_BrutalFXAA", BrutalFXAA);
		FinalShader.SetBool("u_CAS", ContrastAdaptiveSharpening);
		FinalShader.SetMatrix4("u_InverseView", inv_view);
		FinalShader.SetMatrix4("u_InverseProjection", inv_projection);
		FinalShader.SetVector2f("u_Dimensions", glm::vec2(app.GetWidth(), app.GetHeight()));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, PostProcessingFBO.GetTexture());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(0));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(1));

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, InitialTraceFBO->GetTexture(2));

		VAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		VAO.Unbind();

		// CAS

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		if (ContrastAdaptiveSharpening) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, app.GetWidth(), app.GetHeight());

			CAS_Shader.Use();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, app.GetWidth(), app.GetHeight());
			CAS_Shader.SetInteger("u_Texture", 0);
			CAS_Shader.SetFloat("u_SharpenAmount", CAS_SharpenAmount);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, FXAA_Final.GetTexture());

			VAO.Bind();
			glDrawArrays(GL_TRIANGLES, 0, 6);
			VAO.Unbind();
		}

		// Particles

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		if (RenderParticles)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			world->UpdateParticles(&MainCamera, 
				InitialTraceFBO->GetTexture(0),
				SoftShadows ? ShadowFiltered.GetTexture() : ShadowRawTrace.GetTexture(),
				DiffuseDenoiseFBO.GetTexture(), DiffuseDenoiseFBO.GetTexture(1),
				SunDirection, MainCamera.GetPosition(), 
				glm::vec2(app.GetWidth(), app.GetHeight()), 
				DeltaTime);
			glDisable(GL_BLEND);
		}

		RendererUI.RenderQuad(glm::vec2(floor((float)app.GetWidth() / 2.0f), floor((float)app.GetHeight() / 2.0f)), &Crosshair, &OCamera);

		if (app.GetCurrentFrame() % 80 == 0)
		{
			world->GenerateDistanceField();
		}

		if (app.GetCurrentFrame() % 60 == 0)
		{
			world->m_ParticleEmitter.CleanUpList();
		}

		if (app.GetCurrentFrame() % 107 == 0 && PointVolumetricsToggled) {
			std::cout << "\n-- AUTO REUPLOADED VOLUMETRIC VOLUME DATA -- \n";
			Volumetrics::Reupload();
		}

		world->Update(&MainCamera);

		// Finish Frame
		glFinish();
		app.FinishFrame();

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glUseProgram(0);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);

		std::string title = "Voxel RT | "; title += BlockDatabase::GetBlockName(world->GetCurrentBlock()); title += "     "; 
		title += MainPlayer.Freefly ? "   |  FREEFLY" : "";
		title += MainPlayer.DisableCollisions ? "   |  NO COLLISIONS" : "";
		GLClasses::DisplayFrameRate(app.GetWindow(), title);


		float CurrentTime = glfwGetTime();
		DeltaTime = CurrentTime - Frametime;
		Frametime = glfwGetTime();
		DeltaSum += DeltaTime;
		ModifiedWorld = false;
	}

	SaveWorld(world, world_name);
	SoundManager::Destroy();
	delete world;
	return;
	exit(0);
}








// pipeline end.