#pragma once
#include <Windows.h>
#define DIRECTINPUT_VERSION	0x0800
#include<dinput.h>
#include<wrl.h>

//入力
class Input {
public:
	//namespace省略
	template <class Type> using ComPtr = Microsoft::WRL::ComPtr<Type>;
private:
	//DirectInputのインスタンス
	ComPtr<IDirectInput8> directInput;

	//キーボードデバイスのインスタンス
	ComPtr<IDirectInputDevice8> keyboard;

	//全キーの入力状態を取得する
	BYTE key[256] = {};

	//前回の全キーの状態
	BYTE preKey[256] = {};

public:
	//メンバ関数

	// 初期化
	void Initialize(HINSTANCE wc, HWND hwnd);

	// 更新
	void Update();

	/// <summary>
	/// キーの押下をチェック
	/// </summary>
	/// <param name="keyNumber">キー番号(DIK_0等)</param>
	/// <returns>押されているか</returns>
	bool PushKey(BYTE keyNumber);

	/// <summary>
	/// キーのトリガーをチェック
	/// </summary>
	/// <param name="keyNumber">キー番号(DIK_0等)</param>
	/// <returns>トリガーか</returns>
	bool TriggerKey(BYTE keyNumber);
};

