#include "Vector.h"
#include "cmath"
#include "Matrix.h"

Vector3 Vector::Add(const Vector3& vector1, const Vector3& vector2) {
	Vector3 result;

	result.x = vector1.x + vector2.x;
	result.y = vector1.y + vector2.y;
	result.z = vector1.z + vector2.z;

	return result;
}

Vector3 Vector::Subtract(const Vector3& vector1, const Vector3& vector2) {
	Vector3 result;

	result.x = vector1.x - vector2.x;
	result.y = vector1.y - vector2.y;
	result.z = vector1.z - vector2.z;

	return result;
}

Vector3 Vector::Multiply(float scalar, const Vector3& vector) {
	Vector3 result;

	result.x = scalar * vector.x;
	result.y = scalar * vector.y;
	result.z = scalar * vector.z;

	return result;
}

float Vector::Dot(const Vector3& vector1, const Vector3& vector2) {
	float result;

	result = vector1.x * vector2.x + vector1.y * vector2.y + vector1.z * vector2.z;

	return result;
}

float Vector::Length(const Vector3& vector) {
	float result;

	result = sqrtf(Dot(vector, vector));

	return result;
}

Vector3 Vector::Normalize(const Vector3& vector) {
	Vector3 result = { 0 };

	float length = Length(vector);

	if (length == 0) {
		return result;
	}

	result.x = vector.x / length;
	result.y = vector.y / length;
	result.z = vector.z / length;

	return result;
}


Vector3 Vector::Cross(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	
	result.x = v1.y * v2.z - v1.z * v2.y;
	result.y = v1.z * v2.x - v1.x * v2.z;
	result.z = v1.x * v2.y - v1.y * v2.x;

	return result;
}

Vector3 Vector::TransformNormal(const Vector3& v, const Matrix4x4& m) {
	Vector3 result = {
		v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
		v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
		v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]
	};

	return result;
}
