#pragma once
#include "Mesh.h"
#include "Transform.h"
#include "Material.h"
#include <memory>

class GameEntity
{
public:
	GameEntity(std::shared_ptr<Mesh> meshPtr, std::shared_ptr<Material> matPtr);
	std::shared_ptr<Mesh> GetMesh();
	std::shared_ptr<Material> GetMaterial();
	void SetMaterial(std::shared_ptr<Material> _material);

	Transform* GetTransform();
private:
	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<Material> material;
	Transform transform;
};

