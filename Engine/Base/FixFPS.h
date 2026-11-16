#pragma once
#include<Windows.h>
#include<chrono>

class FixFPS {
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();
	void Update();

private:
	//記録時間(FPS固定用)
	std::chrono::steady_clock::time_point reference_;
};

