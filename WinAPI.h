#pragma once
#include<Windows.h>
#include<cstdint>

class WinAPI {
private:
	//ウィンドウハンドル
	HWND hwnd = nullptr;

	//ウィンドウクラスを登録
	WNDCLASS wc{};

public:
	//静的メンバ関数

	//ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);


public:
	//定数

	//クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 終了
	/// </summary>
	void Finalize();

	/// <summary>
	/// メッセージの処理
	/// </summary>
	/// <returns></returns>
	bool ProcessMessage();

	//Getter
	HWND GetHwnd()const { return hwnd; }
	HINSTANCE GETHInstance()const { return wc.hInstance; }
};

