#pragma once
#include<Windows.h>
#include"Vector.h"
#include"Matrix.h"

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
	Matrix4x4 viewMatrix_;

	//射影行列
	Matrix4x4 orthographicMatrix_;

	//動くスピード
	const float moveSpeed_ = 0.1f;
	const float rotateSpeed_ = 0.01f;

public:

	void Initialize();

	void Update(BYTE key[]);

	Matrix4x4 GetViewMatrix() { return viewMatrix_; }

};

