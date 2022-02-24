#include "GameEntity.h"
#include "BufferStructs.h"

using namespace DirectX;

GameEntity::GameEntity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> matPtr)
	:
	mesh(mesh),
	material(matPtr)
{
}

std::shared_ptr<Mesh> GameEntity::GetMesh() { return mesh; }

std::shared_ptr<Material> GameEntity::GetMaterial() { return material; }

void GameEntity::SetMaterial(std::shared_ptr<Material> _material) { material = _material; }

Transform* GameEntity::GetTransform() { return &transform; }