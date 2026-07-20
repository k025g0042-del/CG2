#pragma once
#include "Vector.h"

struct Matrix4x4 {
	float m[4][4];
};

struct Matrix3x3 {
	float m[3][3];
};

static const int kRowHeight = 20;
static const int kColumnWidth = 60;

class Matrix {
private:

public:

	/// <summary>
	/// 行列の加法
	/// </summary>
	/// <param name="m1"></param>
	/// <param name="m2"></param>
	/// <returns></returns>
	static Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);

	/// <summary>
	/// 行列の減法
	/// </summary>
	/// <param name="m1"></param>
	/// <param name="m2"></param>
	/// <returns></returns>
	static Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);

	/// <summary>
	/// 行列の積
	/// </summary>
	/// <param name="m1"></param>
	/// <param name="m2"></param>
	/// <returns></returns>
	static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

	/// <summary>
	/// 逆行列
	/// </summary>
	/// <param name="m"></param>
	/// <returns></returns>
	static Matrix4x4 Inverse(const Matrix4x4& m);

	/// <summary>
	/// 転置行列
	/// </summary>
	/// <param name="m"></param>
	/// <returns></returns>
	static Matrix4x4 Transpose(const Matrix4x4& m);

	/// <summary>
	/// 単位行列の作成
	/// </summary>
	/// <returns></returns>
	static Matrix4x4 MakeIdentity4x4();

	/// <summary>
	/// 平行移動行列
	/// </summary>
	/// <param name="translate">移動量</param>
	/// <returns></returns>
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

	/// <summary>
	/// 拡大縮小行列
	/// </summary>
	/// <param name="scale">拡縮</param>
	/// <returns></returns>
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);

	/// <summary>
	/// 座標変換
	/// </summary>
	/// <param name="vector"></param>
	/// <param name="matrix"></param>
	/// <returns></returns>
	static Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);

	/// <summary>
	/// X軸回転行列
	/// </summary>
	/// <param name="radian">回転</param>
	/// <returns></returns>
	static Matrix4x4 MakeRotateXMatrix(float radian);

	/// <summary>
	/// Y軸回転行列
	/// </summary>
	/// <param name="radian">回転</param>
	/// <returns></returns>
	static Matrix4x4 MakeRotateYMatrix(float radian);

	/// <summary>
	/// Z軸回転行列
	/// </summary>
	/// <param name="radian">回転</param>
	/// <returns></returns>
	static Matrix4x4 MakeRotateZMatrix(float radian);

	/// <summary>
	/// XYZ回転行列
	/// </summary>
	/// <param name="radian">回転</param>
	/// <returns></returns>
	static Matrix4x4 MakeRotateXYZMatrix(Vector3 radian);

	/// <summary>
	/// 3次元アフィン変換行列
	/// </summary>
	/// <param name="scale">拡縮</param>
	/// <param name="rotate">回転</param>
	/// <param name="translate">移動量</param>
	/// <returns></returns>
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

	/// <summary>
	/// 透視投影行列
	/// </summary>
	/// <param name="fovY"></param>
	/// <param name="aspectRatio"></param>
	/// <param name="nearClip"></param>
	/// <param name="farClip"></param>
	/// <returns></returns>
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	/// <summary>
	/// 正射影行列
	/// </summary>
	/// <param name="left"></param>
	/// <param name="top"></param>
	/// <param name="right"></param>
	/// <param name="bottom"></param>
	/// <param name="nearClip"></param>
	/// <param name="farClip"></param>
	/// <returns></returns>
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	/// <summary>
	/// ビューポート変換行列
	/// </summary>
	/// <param name="left"></param>
	/// <param name="top"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	/// <param name="minDepth"></param>
	/// <param name="maxDepth"></param>
	/// <returns></returns>
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

	/// <summary>
	/// ゼロクリア関数
	/// </summary>
	/// <param name="matrix"></param>
	/// <returns></returns>
	static Matrix4x4 ZeroClear4x4(Matrix4x4 &matrix);
};

