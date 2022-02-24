#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Vertex.h"


class Mesh
{
public:
	Mesh(Vertex* vertArray, int numVerts, unsigned int* indexArray, int numIndices);
	Mesh(const char* objFile);
	~Mesh(void);

	Microsoft::WRL::ComPtr<ID3D12Resource> GetVertexBuffer() { return vb; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetIndexBuffer() { return ib; }
	D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() { return vbView; }
	D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() { return ibView; }
	int GetIndexCount() { return numIndices; }

	//void SetBuffersAndDraw(Microsoft::WRL::ComPtr<ID3D12DeviceContext> context);

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> vb;
	Microsoft::WRL::ComPtr<ID3D12Resource> ib;
	int numIndices;

	D3D12_VERTEX_BUFFER_VIEW vbView;
	D3D12_INDEX_BUFFER_VIEW ibView;

	void CreateBuffers(Vertex* vertArray, int numVerts, unsigned int* indexArray, int numIndices);
	void CalculateTangents(Vertex* verts, int numVerts, unsigned int* indices, int numIndices);
};

