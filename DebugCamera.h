#pragma once
#include"Vector.h"
#include"Matrix.h"
#include"Input.h"

/// <summary>
/// デバックカメラ
/// </summary>
class DebugCamera {
private:
	//X,Y,Z軸回りのローカル回転軸
	//Vector3 rotation_ = { 0,0,0 };

	//累積回転行列
	Matrix4x4 matRot_ = Matrix::MakeRotateXYZMatrix({ 0.0f,0.0f,0.0f });

	//ローカル座標
	Vector3 translation_ = { 0,0,-50 };

	//ビュー行列
	Matrix4x4 viewMatrix_ = Matrix::MakeIdentity4x4();

	//射影行列
	Matrix4x4 orthographicMatrix_ = Matrix::MakeIdentity4x4();

	//動くスピード
	const float moveSpeed_ = 0.1f;
	const float rotateSpeed_ = 0.01f;

public:

	void Initialize();

	void Update(Input input);

	Matrix4x4 GetViewMatrix() { return viewMatrix_; }

};

