#pragma once

#include <iostream>
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include "GLClasses/ComputeShader.h"
#include "World.h"
#include <queue>

namespace VoxelRT
{

	class LightNode
	{
	public:

		LightNode(const glm::vec3& position) : m_Position(position)
		{

		}

		glm::vec3 m_Position;
	};

	class LightRemovalNode
	{
	public:

		LightRemovalNode(const glm::vec3& position, int light) : m_Position(position), m_LightValue(light)
		{

		}

		glm::vec3 m_Position;
		uint8_t m_LightValue;
	};




	namespace Volumetrics {

		void CreateVolume(World* world, GLuint SSBO_Blockdata, GLuint AlbedoArray);
		void PropogateVolume();
		void DepropogateVolume();
		uint8_t GetLightValue(const glm::ivec3& p);
		uint8_t GetBlockTypeLightValue(const glm::ivec3& p);
		void SetLightValue(const glm::ivec3& p, uint8_t v, uint8_t block);
		void UploadLight(const glm::ivec3& p, uint8_t v, uint8_t block, bool should_bind);
		void AddLightToVolume(const glm::ivec3& p, uint8_t block);
		void Reupload();
		GLuint GetAverageColorSSBO();
		std::queue<LightNode>& GetLightBFSQueue();
		std::queue<LightRemovalNode>& GetLightRemovalBFSQueue();
		GLuint GetDensityVolume();
		GLuint GetColorVolume();
		void ClearEntireVolume();
	}
}