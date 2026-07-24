#include "ResourceObject.h"

ResourceObject::ResourceObject(ID3D12Resource* resource) {
}

ResourceObject::~ResourceObject() {
	if (resource_) {
		resource_->Release();
	}
}
