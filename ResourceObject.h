#pragma once
#include<d3d12.h>

class ResourceObject {
private:
	ID3D12Resource* resource_;
public:
	ResourceObject(ID3D12Resource* resource) :resource_(resource){}

	~ResourceObject();

	ID3D12Resource* Get() { return resource_; }
};

