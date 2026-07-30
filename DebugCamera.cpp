#include "DebugCamera.h"

void DebugCamera::Initialize() {
	viewMatrix_ = Matrix::MakeIdentity4x4();
	orthographicMatrix_ = Matrix::ZeroClear4x4(orthographicMatrix_);
}

void DebugCamera::Update(BYTE key[]) {
	if (key[DIK_UP]) {

		//カメラ移動ベクトル
		Vector3 move = { 0,0,moveSpeed_ };
		move = Vector::TransformNormal(move, matRot_);

		translation_ += move;
	}

	if (key[DIK_DOWN]) {

		//カメラ移動ベクトル
		Vector3 move = { 0,0,-moveSpeed_ };
		move = Vector::TransformNormal(move, matRot_);

		translation_ += move;
	}

	if (key[DIK_RIGHT]) {

		//カメラ移動ベクトル
		Vector3 move = { moveSpeed_,0,0 };
		move = Vector::TransformNormal(move, matRot_);

		translation_ += move;
	}

	if (key[DIK_LEFT]) {

		//カメラ移動ベクトル
		Vector3 move = { -moveSpeed_,0,0 };
		move = Vector::TransformNormal(move, matRot_);

		translation_ += move;
	}

	if (key[DIK_A]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateYMatrix(-rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	if (key[DIK_D]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateYMatrix(rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	if (key[DIK_W]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateXMatrix(rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	if (key[DIK_S]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateXMatrix(-rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	if (key[DIK_Q]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateZMatrix(-rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	if (key[DIK_E]) {
		Matrix4x4 matRotDelta = Matrix::MakeIdentity4x4();
		matRotDelta = matRotDelta * Matrix::MakeRotateZMatrix(rotateSpeed_);
		matRot_ = matRotDelta * matRot_;
	}

	viewMatrix_ = Matrix::Multiply(Matrix::MakeScaleMatrix({ 1.0f,1.0f,1.0f }),Matrix::Multiply( matRot_,Matrix::MakeTranslateMatrix(translation_)));
	viewMatrix_ = Matrix::Inverse(viewMatrix_);
}
