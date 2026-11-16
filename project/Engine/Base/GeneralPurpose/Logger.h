#pragma once
#include <ostream>

namespace Logger {
	//ログをファイルに書き出す
	void Log(std::ostream& os, const std::string& message);

	void Log(const std::string& message);
};