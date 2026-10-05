#include "Input.h"
#include <cassert>
#define DIRECTINPUT_VERSION	0x0800
#include<dinput.h>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

void Input::Initialize(HINSTANCE wc, HWND hwnd) {
	HRESULT hr;

	//DirectInputの初期化
	IDirectInput8* directInput = nullptr;
	hr = DirectInput8Create(wc, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);
	assert(SUCCEEDED(hr));

	//キーボードデバイスの生成
	IDirectInputDevice8* keyboard = nullptr;
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);

	//入力データ形式のセット
	//標準形式
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	//排他制御レベルのセット
	hr = keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));
}

void Input::Update() {
}
