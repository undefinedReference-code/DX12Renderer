#pragma once
#include <cstdint>
class GeometryGenerator {
public:
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct Vertex {
		DirectX::XMFLOAT3 Position;
	};
};
