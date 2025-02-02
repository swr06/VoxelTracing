#include "World.h"

#include "VolumetricFloodFill.h"
#include "BlockDatabase.h"

void VoxelRT::World::InitializeLightList()
{
	//glGenBuffers(1, &m_LightPositionSSBO);
	//glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightPositionSSBO);
	//glBufferData(GL_SHADER_STORAGE_BUFFER, 1024 * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
	//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &LightChunkDataSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, LightChunkDataSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 64 * 64 * 64 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW); // 1 mb ssbo
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &LightChunkOffsetSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, LightChunkOffsetSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 2 * sizeof(GLuint) * (24 * 8 * 24), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

//void VoxelRT::World::RebufferLightList()
//{
//	//glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightPositionSSBO);
//	//glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::vec4) * (m_LightPositions.size()), &m_LightPositions[0]);
//	//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
//}	

void VoxelRT::World::RebufferLightChunks()
{
	if (LightChunkData.size() > 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, LightChunkDataSSBO);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::vec4) * (LightChunkData.size()), &LightChunkData[0]);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	if (LightChunkOffsets.size() > 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, LightChunkOffsetSSBO);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::ivec2) * (LightChunkOffsets.size()), &LightChunkOffsets[0]);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
}



void VoxelRT::World::InitializeDistanceGenerator()
{
	int work_grp_cnt[3];

	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]);

	printf("max global (total) work group counts x:%i y:%i z:%i\n",
		work_grp_cnt[0], work_grp_cnt[1], work_grp_cnt[2]);

	m_DistanceShaderX.CreateComputeShader("Core/Shaders/ManhattanDistanceX.comp");
	m_DistanceShaderX.Compile();
	m_DistanceShaderY.CreateComputeShader("Core/Shaders/ManhattanDistanceY.comp");
	m_DistanceShaderY.Compile();
	m_DistanceShaderZ.CreateComputeShader("Core/Shaders/ManhattanDistanceZ.comp");
	m_DistanceShaderZ.Compile();

	m_DistanceFieldTexture.CreateTexture(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z, nullptr);
}

void VoxelRT::World::GenerateDistanceField()
{
	std::cout << "\nGenerating Distance Field!\n";

	const int GROUP_SIZE = 32;

	// X PASS

	m_DistanceShaderX.Use();
	m_DistanceShaderX.SetVector3f("u_Dimensions", glm::vec3(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z));
	m_DistanceShaderX.SetInteger("u_BlockData", 1);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_3D, m_DataTexture.GetTextureID());

	glBindImageTexture(0, m_DistanceFieldTexture.GetTextureID(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R8);
	glDispatchCompute(1, WORLD_SIZE_Y / GROUP_SIZE, WORLD_SIZE_Z / GROUP_SIZE);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUseProgram(0);


	// Y PASS

	m_DistanceShaderY.Use();

	glBindImageTexture(0, m_DistanceFieldTexture.GetTextureID(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R8);
	glDispatchCompute(WORLD_SIZE_X / GROUP_SIZE, 1, WORLD_SIZE_Z / GROUP_SIZE);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUseProgram(0);


	// Z PASS

	m_DistanceShaderZ.Use();

	glBindImageTexture(0, m_DistanceFieldTexture.GetTextureID(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R8);
	glDispatchCompute(WORLD_SIZE_X / GROUP_SIZE, WORLD_SIZE_Y / GROUP_SIZE, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUseProgram(0);

	std::cout << "\nFinished Generating Distance Field!\n";
}

void VoxelRT::World::ChangeCurrentlyHeldBlock(bool x)
{
	if (x)
	{
		m_CurrentlyHeldBlock++;

		if (m_CurrentlyHeldBlock >= BlockDatabase::GetNumberOfBlocksInDatabase() || m_CurrentlyHeldBlock >= 127)
		{
			m_CurrentlyHeldBlock = 1;
		}
	}

	else
	{
		if (m_CurrentlyHeldBlock > 0)
		{
			m_CurrentlyHeldBlock--;
		}

		if (m_CurrentlyHeldBlock <= 0)
		{
			m_CurrentlyHeldBlock = BlockDatabase::GetNumberOfBlocksInDatabase() - 1;
		}
	}
}

bool TestAABB3DCollision(const glm::vec3& pos_1, const glm::vec3& dim_1, const glm::vec3& pos_2, const glm::vec3& dim_2)
{
	if (pos_1.x < pos_2.x + dim_2.x &&
		pos_1.x + dim_1.x > pos_2.x &&
		pos_1.y < pos_2.y + dim_2.y &&
		pos_1.y + dim_1.y > pos_2.y &&
		pos_1.z < pos_2.z + dim_2.z &&
		pos_1.z + dim_1.z > pos_2.z)
	{
		return true;
	}

	return false;
}

bool TestRayPlayerCollision(const glm::vec3& ray_block, const glm::vec3& player_pos, glm::vec3 a, bool v, float dt)
{
	return false;

	glm::vec3 pos = player_pos; 

	if (TestAABB3DCollision(pos, glm::vec3(0.75f, 1.5f, 0.75f), ray_block, glm::vec3(1.2f, 1.2f, 1.2f)))
	{
		//if (v) 
		/*{
			{
				glm::vec3 feet_pos = player_pos; feet_pos.y -= 1.0f;
				if (glm::distance(ray_block, feet_pos) < 3.2)
				{
					return true;
				}

				glm::vec3 feet_pos2 = player_pos; feet_pos2.y -= 0.9f;
				if (glm::distance(ray_block, feet_pos2) < 3.2)
				{
					return true;
				}

				if (glm::distance(ray_block, player_pos) < 3.2)
				{
					return true;
				}

				if (glm::distance(ray_block, glm::vec3(player_pos.x, player_pos.y + 0.6f, player_pos.z)) < 3.2)
				{
					return true;
				}
			}
		}*/

		return true;
	}

	return false;

}


glm::vec3 RayBoxIntersection(glm::vec3 ray_origin, glm::vec3 ray_dir, glm::vec3 minpos, glm::vec3 maxpos) {
	using namespace glm;
	vec3 inverse_dir = 1.0f / ray_dir;
	vec3 tbot = inverse_dir * (minpos - ray_origin);
	vec3 ttop = inverse_dir * (maxpos - ray_origin);
	vec3 tmin = min(ttop, tbot);
	vec3 tmax = max(ttop, tbot);
	vec2 traverse = glm::max(glm::vec2(tmin.x), glm::vec2(tmin.y, tmin.z));
	float traverselow = max(traverse.x, traverse.y);
	traverse = min(glm::vec2(tmax.x), glm::vec2(tmax.y, tmax.z));
	float traversehi = min(traverse.x, traverse.y);
	return vec3(float(traversehi > max(traverselow, 0.0f)), traversehi, traverselow);
}

// DDA Voxel Traversal Algorithm ->

bool VoxelRT::World::Raycast(uint8_t op, glm::vec3 pos, const glm::vec3& dir, const glm::vec3& acceleration, bool is_falling, float dt)
{
	glm::vec3 OriginalOrigin = pos;

	bool ret_val = false;



	glm::vec3 position = pos;
	const glm::vec3& direction = dir;
	int max = 48; // block reach

	bool PlacedSomething = false;

	glm::vec3 sign;

	for (int i = 0; i < 3; ++i)
		sign[i] = direction[i] > 0;

	for (int i = 0; i < max; ++i)
	{
		glm::vec3 tvec = (floor(position + sign) - position) / direction;
		float t = std::min(tvec.x, std::min(tvec.y, tvec.z));

		position += direction * (t + 0.001f);

		if (!((int)floor(position.x) >= WORLD_SIZE_X || (int)floor(position.y) >= WORLD_SIZE_Y || (int)floor(position.z) >= WORLD_SIZE_Z ||
			(int)floor(position.x) <= 0 || (int)floor(position.y) <= 0 || (int)floor(position.z) <= 0 ))
		{
			Block ray_block = GetBlock((int)position.x, (int)position.y, (int)position.z);

			if (ray_block.block != 0)
			{
				glm::vec3 normal;

				for (int j = 0; j < 3; ++j)
				{
					normal[j] = (t == tvec[j]);

					if (sign[j])
					{
						normal[j] = -normal[j];
					}
				}

				if (op == 1)
				{
					position = position + normal;
				}

				position = glm::floor(position);

				if ((int)floor(position.x) >= WORLD_SIZE_X || (int)floor(position.y) >= WORLD_SIZE_Y || (int)floor(position.z) >= WORLD_SIZE_Z ||
					(int)floor(position.x) <= 0 || (int)floor(position.y) <= 0 || (int)floor(position.z) <= 0)
				{ 
					return false; 
				}

				if (op == 1)
				{
					auto& LightRemovalBFS = Volumetrics::GetLightRemovalBFSQueue();

					{
						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y), floor(position.z)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x + 1),
							floor(position.y),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x + 1), floor(position.y), floor(position.z)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x - 1),
							floor(position.y),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x - 1), floor(position.y), floor(position.z)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y + 1),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y + 1), floor(position.z)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y - 1),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y - 1), floor(position.z)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y),
							floor(position.z + 1)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y), floor(position.z + 1)))));

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y),
							floor(position.z - 1)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y), floor(position.z - 1)))));

					}





					uint8_t editblock = m_CurrentlyHeldBlock;

					if (BlockDatabase::GetBlockEmissiveTexture(editblock) >= 0) {
						std::cout << "\nLAMP PLACED";
						VoxelRT::Volumetrics::AddLightToVolume(glm::ivec3((int)position.x, (int)position.y, (int)position.z), editblock);
					
						this->InsertToLightList(glm::vec3(
							floor(position.x),
							floor(position.y),
							floor(position.z)));
					}


					// Make sure that the placed block doesn't intersect with the player 
					auto PrevBlock = GetBlock((int)position.x, (int)position.y, (int)position.z);
					auto PlayerUnderBlock = GetBlock((int)OriginalOrigin.x, ((int)OriginalOrigin.y) - 1.0f, (int)OriginalOrigin.z);
					auto PlayerUnderBlock2 = GetBlock((int)OriginalOrigin.x, (int)(OriginalOrigin.y - 1.0f), (int)OriginalOrigin.z);
					auto PlayerUnderBlock3 = GetBlock((int)OriginalOrigin.x, (int)(OriginalOrigin.y - 1.1f), (int)OriginalOrigin.z);

					if (PrevBlock.block != 0 || glm::distance(glm::floor(position), (glm::vec3(OriginalOrigin.x, OriginalOrigin.y, OriginalOrigin.z))) < 1.25f) {
						return false;
					}

					if (PlayerUnderBlock.block == 0 || PlayerUnderBlock2.block == 0 || PlayerUnderBlock3.block == 0) {
						if (glm::distance(glm::floor(position), glm::vec3(OriginalOrigin.x, OriginalOrigin.y - 1.0f, OriginalOrigin.z)) < 1.35f) {
							return false;
						}
					}
					//


					PlacedSomething = PrevBlock.block != editblock;

					if (PlacedSomething)
					{
						SoundManager::PlayBlockSound(editblock, position, false);
						ret_val = true;
					}



					SetBlock((int)position.x, (int)position.y, (int)position.z, { editblock });

					if (m_Buffered && PlacedSomething)
					{
					
						glBindTexture(GL_TEXTURE_3D, m_DataTexture.GetTextureID());
						glTexSubImage3D(GL_TEXTURE_3D, 0, (int)position.x, (int)position.y, (int)position.z, 1, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &editblock);
						GenerateDistanceField();
					}

				}

				else if (op == 0)
				{
					ret_val = true; 

					float y1 = rand() % 2 == 0 ? 0.05f : 0.075f;
					float y2 = y1 == 0.05f ? 0.1f : 0.113f;
					glm::vec3 x = rand() % 2 == 0 ? glm::vec3(y1, 1, y2) : glm::vec3(y2, 1, y1);
					glm::vec3 particle_pos;
					particle_pos.x = floor(position.x);
					particle_pos.y = position.y + 0.4f;
					particle_pos.z = floor(position.z);
					particle_pos.x += 0.5f;
					particle_pos.z += 0.5f;
					m_ParticleEmitter.EmitParticlesAt(glm::vec3(floor(position.x), floor(position.y), floor(position.z)), 2.75f, 40,
					particle_pos, glm::vec3(5, 5, 5), x, GetBlock((int)position.x, (int)position.y, (int)position.z).block);

					SoundManager::PlayBlockSound(GetBlock((int)position.x, (int)position.y, (int)position.z).block, position, false);



					uint8_t edited_block = GetBlock((int)position.x, (int)position.y, (int)position.z).block;
					auto& LightRemovalBFS = Volumetrics::GetLightRemovalBFSQueue();
					auto& LightPropogateBFS = Volumetrics::GetLightBFSQueue();


					SetBlock((int)position.x, (int)position.y, (int)position.z, { 0 });


					if (BlockDatabase::HasEmissiveTexture(edited_block))
					{
						std::cout << "\nLAMP BROKEN!\n";

						LightRemovalBFS.push(LightRemovalNode(glm::vec3(
							floor(position.x),
							floor(position.y),
							floor(position.z)),
							Volumetrics::GetLightValue(glm::ivec3(floor(position.x), floor(position.y), floor(position.z)))));

						Volumetrics::SetLightValue(glm::ivec3(
							floor(position.x),
							floor(position.y),
							floor(position.z)), 0, 0);

						Volumetrics::UploadLight(glm::ivec3(
							floor(position.x),
							floor(position.y),
							floor(position.z)), 0, 0, true);
					}

					// Propogate!
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x + 1),
						floor(position.y),
						floor(position.z))));
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x - 1),
						floor(position.y),
						floor(position.z))));
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x),
						floor(position.y + 1),
						floor(position.z))));
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x),
						floor(position.y - 1),
						floor(position.z))));
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x),
						floor(position.y),
						floor(position.z + 1))));
					LightPropogateBFS.push(LightNode(glm::vec3(
						floor(position.x),
						floor(position.y),
						floor(position.z - 1))));

					if (m_Buffered)
					{
						uint8_t editblock = 0;

						glBindTexture(GL_TEXTURE_3D, m_DataTexture.GetTextureID());
						glTexSubImage3D(GL_TEXTURE_3D, 0, (int)position.x, (int)position.y, (int)position.z, 1, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &editblock);
						GenerateDistanceField();
					}

					this->RemoveFromLightList(glm::vec3(
						floor(position.x),
						floor(position.y),
						floor(position.z)));
				}

				else if (op == 2)
				{
					uint8_t block = GetBlock((int)position.x, (int)position.y, (int)position.z).block;

					if (block > 0)
					{
						m_CurrentlyHeldBlock = block;
					}

					return ret_val;
				}

				glBindTexture(GL_TEXTURE_3D, 0);

				for (int PropogateIterations = 0; PropogateIterations < 4; PropogateIterations++) {
					Volumetrics::DepropogateVolume();
					Volumetrics::PropogateVolume();
				}

				return ret_val;
			}
		}
	}

	return ret_val;
}

glm::ivec4 VoxelRT::World::RaycastDetect(const glm::vec3& pos, const glm::vec3& dir)
{
	glm::vec3 position = pos;
	const glm::vec3& direction = dir;
	int max = 48; // block reach

	glm::vec3 sign;

	for (int i = 0; i < 3; ++i)
		sign[i] = direction[i] > 0;

	for (int i = 0; i < max; ++i)
	{
		glm::vec3 tvec = (floor(position + sign) - position) / direction;
		float t = std::min(tvec.x, std::min(tvec.y, tvec.z));

		position += direction * (t + 0.001f);

		if (!((int)floor(position.x) >= WORLD_SIZE_X || (int)floor(position.y) >= WORLD_SIZE_Y || (int)floor(position.z) >= WORLD_SIZE_Z ||
			(int)floor(position.x) <= 0 || (int)floor(position.y) <= 0 || (int)floor(position.z) <= 0))
		{
			Block ray_block = GetBlock((int)position.x, (int)position.y, (int)position.z);

			if (ray_block.block != 0)
			{
				glm::vec3 normal;

				for (int j = 0; j < 3; ++j)
				{
					normal[j] = (t == tvec[j]);

					if (sign[j])
					{
						normal[j] = -normal[j];
					}
				}

				position = glm::floor(position);

				if ((int)floor(position.x) >= WORLD_SIZE_X || (int)floor(position.y) >= WORLD_SIZE_Y || (int)floor(position.z) >= WORLD_SIZE_Z ||
					(int)floor(position.x) <= 0 || (int)floor(position.y) <= 0 || (int)floor(position.z) <= 0)
				{
					return glm::ivec4(-1);
				}

				uint8_t block = GetBlock((int)position.x, (int)position.y, (int)position.z).block;
				return glm::ivec4(glm::ivec3((int)position.x, (int)position.y, (int)position.z), block);
			}
		}
	}
}

// Renders particles
void VoxelRT::World::UpdateParticles(FPSCamera* cam, GLuint pos_texture, GLuint shadow_tex, GLuint diff, GLuint diff2, const glm::vec3& sdir, const glm::vec3& player_pos, const glm::vec2& dims, float dt)
{
	m_ParticleEmitter.OnUpdateAndRender(cam, m_WorldData, pos_texture, shadow_tex, diff, diff2, sdir, player_pos, dims, dt);
}

void VoxelRT::World::RepropogateLPV_()
{
	std::cout << "\n\n----REPROPOGATING LPV----\n\n";

	Volumetrics::ClearEntireVolume();

	for (auto& e : LightChunkData) {
		uint8_t block_at = this->GetBlock(glm::ivec3(glm::floor(glm::vec3(e)))).block;
		Volumetrics::AddLightToVolume(e, block_at);
	}

	for (int PropogationIterations = 0; PropogationIterations < 4; PropogationIterations++) {
		Volumetrics::PropogateVolume();
	}

	Volumetrics::Reupload();

	std::cout << "\n\n----REPROPOGATED LPV----\n\n";
}

