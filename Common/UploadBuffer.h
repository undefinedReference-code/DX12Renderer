#pragma once

#include "d3dUtil.h"
template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);
		// Constant buffer elements need to be multiples of 256 bytes.
		// This is because the hardware can only view constant data
		// at m*256 byte offsets and of n*256 byte lengths.
		// typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
		// UINT64 OffsetInBytes; // multiple of 256
		// UINT SizeInBytes; // multiple of 256
		// } D3D12_CONSTANT_BUFFER_VIEW_DESC;
		if (isConstantBuffer)
			mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)));

		/*
		 * Upon successful mapping:
		 *   - `mMappedData` points to the starting address of CPU-accessible memory associated with the GPU upload heap.
		 *   - The CPU can directly write data (e.g., vertices, constants) through this pointer without extra copying.
		 *   - Writes are visible to the GPU (since the resource resides in a D3D12_HEAP_TYPE_UPLOAD heap).
		 *
		 * Important considerations:
		 *   1. This mapping is persistent; no need to remap before each write operation.
		 *   2. Ensure writes do not exceed buffer bounds and avoid writing while the GPU is reading (synchronization required).
		 *   3. `mMappedData` remains valid until Unmap() is called.
		 *   4. `reinterpret_cast<void**>` safely converts pointer types to match the Map() interface signature.
		 */
		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
		// We do not need to unmap until we are done with the resource.
		// However, we must not write to the resource while it is in use by
		// the GPU (so we must use synchronization techniques).
	}

	UploadBuffer(const UploadBuffer& rhs) = delete;

	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);
		mMappedData = nullptr;
	}

	ID3D12Resource* Resource()const
	{
		return mUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;
	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};