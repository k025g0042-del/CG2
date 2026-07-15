#pragma once

struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

struct Vector3 {
	float x;
	float y;
	float z;
};

struct Vector2 {
	float x;
	float y;
};

class Vector {
private:
	static const int kColumnWidth = 60;
public:
	/// <summary>
	/// 加算
	/// </summary>
	/// <param name="vector1"></param>
	/// <param name="vector2"></param>
	/// <returns></returns>
	static Vector3 Add(const Vector3& vector1, const Vector3& vector2);
	
	/// <summary>
	/// 減算
	/// </summary>
	/// <param name="vector1"></param>
	/// <param name="vector2"></param>
	/// <returns></returns>
	static Vector3 Subtract(const Vector3& vector1, const Vector3& vector2);

	/// <summary>
	/// スカラー倍
	/// </summary>
	/// <param name="scalar"></param>
	/// <param name="vector"></param>
	/// <returns></returns>
	static Vector3 Multiply(float scalar, const Vector3& vector);

	/// <summary>
	/// 内積
	/// </summary>
	/// <param name="vector1"></param>
	/// <param name="vector2"></param>
	/// <returns></returns>
	static float Dot(const Vector3& vector1, const Vector3& vector2);

	/// <summary>
	/// 長さ(ノルム)
	/// </summary>
	/// <param name="vector"></param>
	/// <returns></returns>
	static float Length(const Vector3& vector);

	/// <summary>
	/// 正規化
	/// </summary>
	/// <param name="vector"></param>
	/// <returns></returns>
	static Vector3 Normalize(const Vector3& vector);

	/// <summary>
	/// クロス積
	/// </summary>
	/// <param name="v1"></param>
	/// <param name="v2"></param>
	/// <returns></returns>
	static Vector3 Cross(const Vector3& v1, const Vector3& v2);
};

