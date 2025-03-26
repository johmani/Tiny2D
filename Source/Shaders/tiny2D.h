#ifndef HEADER_TINY2D_H
#define HEADER_TINY2D_H

#ifdef SPIRV
#define VK_PUSH_CONSTANT [[vk::push_constant]]
#define VK_BINDING(reg,dset) [[vk::binding(reg,dset)]]
#else
#define VK_PUSH_CONSTANT
#define VK_BINDING(reg,dset) 
#endif

float4x4 ConstructTransformMatrix(float3 position, float4 rotation, float3 scale)
{
	rotation = normalize(rotation);

	float x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;

	float4x4 rotationMatrix = float4x4(
		1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), 0,
		2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), 0,
		2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), 0,
		0, 0, 0, 1
	);

	float4x4 scaleMatrix = float4x4(
		scale.x, 0, 0, 0,
		0, scale.y, 0, 0,
		0, 0, scale.z, 0,
		0, 0, 0, 1
	);

	float4x4 translationMatrix = float4x4(
		1, 0, 0, position.x,
		0, 1, 0, position.y,
		0, 0, 1, position.z,
		0, 0, 0, 1
	);

	return mul(translationMatrix, mul(rotationMatrix, scaleMatrix));
}

#endif // HEADER_TINY2D_H
