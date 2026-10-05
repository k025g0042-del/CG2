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
	ComPtr<IDirectInputDevice8> keyboard;
public:
	//メンバ関数

	// 初期化
	void Initialize(HINSTANCE wc,HWND hwnd);

	// 更新
	void Update();
};

