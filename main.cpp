#include<Windows.h>
#include<sal.h>
#include<cstdint>
#include<string>
#include<format>
#include<filesystem>
#include<fstream>
#include<chrono>
#include <iostream>

#include<d3d12.h>
#include<dxgi1_6.h>
#include<cassert>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

#include <dbghelp.h>
#pragma comment(lib,"Dbghelp.lib")
#include<strsafe.h>

#include<dxgidebug.h>
#pragma comment(lib,"dxguid.lib")

#include<dxcapi.h>
#pragma comment(lib,"dxcompiler.lib")

#include "Matrix.h"

#ifdef USE_IMGUI
#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_dx12.h"
#include"externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif //USE_IMGUI 

#include"externals/DirectXTex/DirectXTex.h"

#include"externals/DirectXTex/d3dx12.h"
#include<vector>

#define _USE_MATH_DEFINES
#include<math.h>

#include <sstream>

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight {
	Vector4 color; //ライトの色
	Vector3 direction; //ライトの向き
	float intensity; //輝度
};

struct MaterialData {
	std::string textureFilePath;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};


//ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif // USE_IMGUI	

	//メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		//ウィンドウが破棄された
	case WM_DESTROY:
		//OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	//標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

//ログをファイルに書き出す
void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	//時刻を取得して、時刻を名前に入れたファイルを作成。Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	//processId(このexeのId)とクラッシュ(例外)の発生したthreadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	//設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	//Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	//他に関連付けられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}

/// <summary>
/// CompileShader関数
/// </summary>
/// <param name="filePath">CompilerするShaderファイルへのパス</param>
/// <param name="profile">Compilerに使用するProfile</param>
/// 初期化で生成したもの三つ
/// <param name="dxcUtils"></param>
/// <param name="dxcCompiler"></param>
/// <param name="includeHandler"></param>
/// <returns></returns>
IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler) {
	//これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	//hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	//読めなかったら止める
	assert(SUCCEEDED(hr));

	//読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	//UTF8の文字コードであることを通知
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	//Compileする
	/*行ごとに
	コンパイル対象のhlslファイル名、
	エントリーポイントの指定。基本的にmain以外にはしない、
	ShaderProfileの設定、
	デバック用の情報を埋め込む、
	最適化を外しておく、
	メモリレイアウトは行優先*/
	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E",L"main",
		L"-T",profile,
		L"-Zi",L"-Qembed_debug",
		L"-Od",
		L"-Zpr",
	};

	//実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	/*順に読み込んだファイル、コンパイルオプション、コンパイルオプションの数、includeが含まれた諸々、コンパイル結果*/
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));

	//コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	//警告・エラーが出ていないか確認し、出ていたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());

		//警告・エラーダメ絶対
		assert(false);
	}

	//Compile結果を受け取って返す
	//コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	//成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

	//もう使わないリソースを開放
	shaderSource->Release();
	shaderResult->Release();

	//実行用のバイナリを返却
	return shaderBlob;

}

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {

	HRESULT hr;

	//頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};

	//UploadHeapを使う
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	//頂点リソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};

	//バッファリソース。テクスチャの場合はまた別の設定をする
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

	//リソースのサイズ。今回はVector4を3頂点分
	resourceDesc.Width = sizeInBytes;

	//バッファの場合はこれらは1にする決まり
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;

	//バッファの場合はこれにする決まり
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;


	//実際に頂点リソースを作る
	ID3D12Resource* resource = nullptr;
	hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	//ディスクリプタヒープの生成
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};

	//レンダーターゲットビュー用
	descriptorHeapDesc.Type = heapType;

	//ダブルバッファ用に二つ。多くても問題ない
	descriptorHeapDesc.NumDescriptors = numDescriptors;

	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr;

	hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));

	//ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

/// <summary>
/// Textureデータを読む
/// </summary>
/// <param name="filePath"></param>
/// <returns></returns>
DirectX::ScratchImage LoadTexture(const std::string& filePath) {

	//テクスチャファイルを読んでプログラムで使えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	//ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	//ミップマップ付きのデータを返す
	return mipImages;
}

/// <summary>
/// 読み込んだTexture情報をもとにTextureResourceを作る
/// </summary>
/// <param name="device"></param>
/// <param name="metadata"></param>
/// <returns></returns>
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	//metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};

	//Textureの幅
	resourceDesc.Width = UINT(metadata.width);

	//Textureの高さ
	resourceDesc.Height = UINT(metadata.height);

	//mipmapの数
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);

	//奥行き or 配列Textureの配列数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);

	//TextureのFormat
	resourceDesc.Format = metadata.format;

	//サンプリングカウント。1固定。
	resourceDesc.SampleDesc.Count = 1;

	//Textureの次元数。普段使ってるのは二次元
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	//利用するHeapの設定。非常に特殊な運用。
	D3D12_HEAP_PROPERTIES heapProperties{};

	//細かい設定を行う
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	//Resourceを生成する
	ID3D12Resource* resource = nullptr;

	/*順に、Heapの設定、Heapの特殊な設定。特になし、
	Resourceの設定、初回のResourceState。Texture破棄本読むだけ
	Clear最適値。使わないのでnullptr、作成するResourceポインタへのポインタ*/
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr, IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));
	return resource;
}

/// <summary>
/// TextureResourceにデータを転送する
/// </summary>
/// <param name="texture"></param>
/// <param name="mipImages"></param>
[[nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

	//Tetureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENRIC_READへResourceStateを変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
	//生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};

	//Textureの幅
	resourceDesc.Width = width;

	//Textureの高さ
	resourceDesc.Height = height;

	//mipmapの数
	resourceDesc.MipLevels = 1;

	//奥行きor配列Textureの配列数
	resourceDesc.DepthOrArraySize = 1;

	//DepthStencilとして利用可能なフォーマット
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//サンプリングカウント。1固定。
	resourceDesc.SampleDesc.Count = 1;

	//2次元
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	//DepthStencilとして使う通知
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	//利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};

	//VRAM上に作る
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValus{};

	//1.0f(最大値)でクリア
	depthClearValus.DepthStencil.Depth = 1.0f;

	//フォーマット。Resourceと合わせる
	depthClearValus.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//Resourceの生成
	ID3D12Resource* resource = nullptr;
	/*順に、Heapの設定、Heapの特殊な設定。特になし、Resourceの設定、
	深度値を書き込む状態にしておく、Clear最適値、作成するResourceポインタへのポインタ*/
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValus,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

/// <summary>
/// mtlファイルを読む関数
/// </summary>
/// <param name="directoryPath"></param>
/// <param name="filename"></param>
/// <returns></returns>
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	//1.　中で必要となる変数の宣言

	//構築するMaterialData
	MaterialData materialData;

	//ファイルから読んだ1行を格納する物
	std::string line;

	//2.　ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);

	//開けなかったら止める
	assert(file.is_open());

	//3.　実際にファイルを読み、MaterialDataを構築していく
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		//identifierに応じた処理
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			//連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	file.close();

	//4.　MaterialDataを返す
	return materialData;
}

/// <summary>
/// Objファイルを読む関数
/// </summary>
/// <param name="directryPath"></param>
/// <param name="filename"></param>
/// <returns></returns>
ModelData LoadObjFile(const std::string& directryPath, const std::string& filename) {
	//1.　中で必要となる変数の宣言

	//構築するModelData
	ModelData modelData;

	//位置
	std::vector<Vector4>positions;

	//法線
	std::vector<Vector3>normals;

	//テクスチャ座標
	std::vector<Vector2>texcoords;

	//ファイルから読んだ1行を格納する物
	std::string line;

	//2.　ファイルを開く
	std::ifstream file(directryPath + "/" + filename);

	//開けなかったら止める
	assert(file.is_open());

	//3.　実際にファイルを読み、ModelDataを構築していく
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);

		//先端の識別子を読む
		s >> identifier;

		//identifierに応じた処理を行う
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.x *= -1.0f;
			position.w = 1.0f;
			positions.push_back(position);
		} else if (identifier == "vt") {
			Vector2 texccord;
			s >> texccord.x >> texccord.y;
			texccord.y = 1.0f - texccord.y;
			texcoords.push_back(texccord);
		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1.0f;
			normals.push_back(normal);
		} else if (identifier == "f") {
			VertexData triangle[3];
			//面は三角形限定。その他は未対応
			for (int32_t faceVertex = 0;faceVertex < 3;++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				//頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0;element < 3;++element) {
					std::string index;

					//区切りでインデックスを読んでいく
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				//要素へのIndexから、実際の要素の値を取得して、頂点を構築する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				/*VertexData vertex = { position,texcoord,normal };
				modelData.vertices.push_back(vertex);*/

				triangle[faceVertex] = { position,texcoord,normal };
			}

			//頂点を逆順で登録することで、周り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		} else if (identifier == "mtllib") {
			//materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			//基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directryPath, materialFilename);
		}
	}

	file.close();

	//4.　ModelDataを返す
	return modelData;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	//COMの初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);

	//誰も捕捉しなかった場合に(Unhandled)、補足する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	//ログのディレクトリを用意
	std::filesystem::create_directory("logs");

	//ログファイル生成
	//現在時刻を取得(UTC時刻)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	//ログファイルの名前にコンマ秒は不要なため、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);


	//日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(),nowSeconds };

	//formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	//時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	//ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);
	//LogFileCreate();

	//ウィンドウクラスを登録
	WNDCLASS wc{};
	//ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	//ウィンドウクラス名(何でもよい)
	wc.lpszClassName = L"CG2WindowClass";
	//インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	//ウィンドウクラスを登録する
	RegisterClass(&wc);

	//クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	//クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウの生成
	/*
	順に、利用するクラス名、タイトルバーの文字(何でもよい)、
	よく見るウィンドウスタイル、表示X座標(windowsに任せる)、
	表示Y座標(windowsOSに任せる)、ウィンドウ横幅、ウィンドウ縦幅、
	親ウィンドウハンドル、メニューハンドル、インスタンスハンドル、
	オプション
	*/

	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);

	//ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	//ログ記録
	Log("Window Create!\n");

	//DebugLayer(デバックレイヤー)
#ifdef _DEBUG

	ID3D12Debug1* debugController = nullptr;

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		//デバックレイヤーを有効化する
		debugController->EnableDebugLayer();

		//さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif // DEBUG


	//DXGIファクトリーの生成
	IDXGIFactory7* dxgiFactory = nullptr;
	/*HRESULTはWindows系のエラーコードであり、
	  関数が成功したかどうかをSUCCEEDEDマクロで判定できる*/
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	//初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	//使用するアダプタ用の変数。最初にnullptrを入れておく
	IDXGIAdapter4* useAdapter = nullptr;

	//良い順にアダプタを頼む
	for (UINT i = 0;dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND;++i) {
		//アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));//取得できないのは一大事

		//ソフトウェアアダプタ出なければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			//採用したアダプタの情報をログに出力。wstringの方なので注意
			Log(ConvertString(std::format(L"Use Adapater:{}\n", adapterDesc.Description)));
			break;
		}

		//ソフトウェアアダプタの場合は見なかったことにする
		useAdapter = nullptr;
	}

	//適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	//D3D12Deviceの生成
	ID3D12Device* device = nullptr;

	//機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0 };
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };

	//高い順に生成できるか試していく
	for (size_t i = 0;i < _countof(featureLevels);++i) {
		//採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));

		//指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			//生成できたのでログ出力を行ってループを抜ける
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	//デバイスの生成が上手くいかなかったので起動できない
	assert(device != nullptr);

	//初期化完了のログを出す
	Log("Complete create D3D12Device!!!\n");

#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;

	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		//ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		//エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		//警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		//解放
		infoQueue->Release();

		//抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			//Windows11でのDXGIデバックレイヤーとDX12デバックレイヤーの相互作用バグによるエラーメッセージ
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		//抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		//指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);

	}
#endif // _DEBUG


	//コマンドキューを生成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	//コマンドキューの生成が上手くいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	//コマンドアロケータの生成が上手くいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	//コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//スワップチェーンを生成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	//画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Width = kClientWidth;
	//画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight;
	//色の形式
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//マルチサンプルしない
	swapChainDesc.SampleDesc.Count = 1;
	//描画のターゲットとして利用する
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	//ダブルバッファ
	swapChainDesc.BufferCount = 2;
	//モニタにうつしたら、中身を破棄
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	//コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));

	//RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	//SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//DescriptorSizeを取得しておく
	const uint32_t desriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t desriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t desriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	//SwapChainからResourceを引っ張ってくる
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	//上手く取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	//RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	//出力結果をSRGBに変換して書き込む
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	//2dテクスチャとして書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	//ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	//RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	//まず一つ目を作る。一つ目は最初のところに作る。作る場所を指定する必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	//二つ目のディスクリプタハンドルを得る(自力で)
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//二つ目を作る
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	//初期値0でFenceを作る
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	//FenceのSignalを待つためのイベントを用意する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	//dxcCompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));


	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	//RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	//RootParameter作成。複数設定できるので配列。PixelShaderのMaterialとVertexShaderのTransform
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	//CBVを使う
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	//PixelShaderで使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	//レジスタ番号0とバインド
	rootParameters[0].Descriptor.ShaderRegister = 0;

	//CBVを使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	//VertexShaderで使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	//レジスタ番号0を使う
	rootParameters[1].Descriptor.ShaderRegister = 0;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};

	//0から始まる
	descriptorRange->BaseShaderRegister = 0;

	//数は一つ
	descriptorRange->NumDescriptors = 1;

	//SRVを使う
	descriptorRange->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

	//Offsetを自動計算
	descriptorRange->OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	//DescriptorTableを使う
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

	//PixelShaderで使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	//Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;

	//Tableで利用する数
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	//CBVを使う
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	//PixelShaderで使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	//レジスタ番号1を使う
	rootParameters[3].Descriptor.ShaderRegister = 1;

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};

	//バイリニアフィルタ
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	//0~1の範囲外をリピート
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	//比較しない
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	//ありったけのMipmapを使う
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;

	//レジスタ番号0を使う
	staticSamplers[0].ShaderRegister = 0;

	//PixelShaderで使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	//ルートパラメータ配列へのポインタ
	descriptionRootSignature.pParameters = rootParameters;

	//配列の長さ
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	//シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	//バイナリを元に生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	//InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	//BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};

	//全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	//RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	//裏側(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	//Shaderをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(pixelShaderBlob != nullptr);

	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	//Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;

	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;


	//PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};

	//RootSignature
	graphicsPipelineStateDesc.pRootSignature = rootSignature;

	//InputLayout
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;

	//VertexShader
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),vertexShaderBlob->GetBufferSize() };

	//PixelShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),pixelShaderBlob->GetBufferSize() };

	//BlendState
	graphicsPipelineStateDesc.BlendState = blendDesc;

	//RasterizerState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	//書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	//利用するトロポジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	//どのように画面に色を打ち込むかの設定(気にしなくてよい)
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	//DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

#pragma region 球の頂点データ
	/*
	//球の分割数
	const uint32_t kSubdivision = 16;

	//頂点リソースを作る
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * 6 * kSubdivision * kSubdivision);

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	//リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	//使用するリソースのサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * 6 * kSubdivision * kSubdivision;

	//1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	//頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;

	//書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	//経度分割1つ分の角度　φd
	const float kLonEvery = float(M_PI) * 2.0f / float(kSubdivision);

	//緯度分割1つ分の角度　θd
	const float kLatEvery = float(M_PI) / float(kSubdivision);

	//緯度の方向に分割
	for (int latIndex = 0;latIndex < kSubdivision;++latIndex) {
		float lat = -float(M_PI) / 2.0f + kLatEvery * latIndex;//θ

		//経度の方向に分割しながら線を描く
		for (int lonIndex = 0;lonIndex < kSubdivision;++lonIndex) {
			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;
			float lon = lonIndex * kLonEvery;//φ

			//頂点にデータを入力する。基準点a
			vertexData[start].position.x = std::cos(lat) * std::cos(lon);
			vertexData[start].position.y = std::sin(lat);
			vertexData[start].position.z = std::cos(lat) * std::sin(lon);
			vertexData[start].position.w = 1.0f;
			vertexData[start].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start].normal.x = vertexData[start].position.x;
			vertexData[start].normal.y = vertexData[start].position.y;
			vertexData[start].normal.z = vertexData[start].position.z;

			vertexData[start + 1].position.x = std::cos(lat + kLatEvery) * std::cos(lon);
			vertexData[start + 1].position.y = std::sin(lat + kLatEvery);
			vertexData[start + 1].position.z = std::cos(lat + kLatEvery) * std::sin(lon);
			vertexData[start + 1].position.w = 1.0f;
			vertexData[start + 1].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start + 1].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 1].normal.x = vertexData[start + 1].position.x;
			vertexData[start + 1].normal.y = vertexData[start + 1].position.y;
			vertexData[start + 1].normal.z = vertexData[start + 1].position.z;

			vertexData[start + 2].position.x = std::cos(lat) * std::cos(lon + kLonEvery);
			vertexData[start + 2].position.y = std::sin(lat);
			vertexData[start + 2].position.z = std::cos(lat) * std::sin(lon + kLonEvery);
			vertexData[start + 2].position.w = 1.0f;
			vertexData[start + 2].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 2].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start + 2].normal.x = vertexData[start + 2].position.x;
			vertexData[start + 2].normal.y = vertexData[start + 2].position.y;
			vertexData[start + 2].normal.z = vertexData[start + 2].position.z;

			vertexData[start + 3].position.x = std::cos(lat) * std::cos(lon + kLonEvery);
			vertexData[start + 3].position.y = std::sin(lat);
			vertexData[start + 3].position.z = std::cos(lat) * std::sin(lon + kLonEvery);
			vertexData[start + 3].position.w = 1.0f;
			vertexData[start + 3].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 3].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start + 3].normal.x = vertexData[start + 3].position.x;
			vertexData[start + 3].normal.y = vertexData[start + 3].position.y;
			vertexData[start + 3].normal.z = vertexData[start + 3].position.z;

			vertexData[start + 4].position.x = std::cos(lat + kLatEvery) * std::cos(lon);
			vertexData[start + 4].position.y = std::sin(lat + kLatEvery);
			vertexData[start + 4].position.z = std::cos(lat + kLatEvery) * std::sin(lon);
			vertexData[start + 4].position.w = 1.0f;
			vertexData[start + 4].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start + 4].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 4].normal.x = vertexData[start + 4].position.x;
			vertexData[start + 4].normal.y = vertexData[start + 4].position.y;
			vertexData[start + 4].normal.z = vertexData[start + 4].position.z;

			vertexData[start + 5].position.x = std::cos(lat + kLatEvery) * std::cos(lon + kLonEvery);
			vertexData[start + 5].position.y = std::sin(lat + kLatEvery);
			vertexData[start + 5].position.z = std::cos(lat + kLatEvery) * std::sin(lon + kLonEvery);
			vertexData[start + 5].position.w = 1.0f;
			vertexData[start + 5].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 5].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 5].normal.x = vertexData[start + 5].position.x;
			vertexData[start + 5].normal.y = vertexData[start + 5].position.y;
			vertexData[start + 5].normal.z = vertexData[start + 5].position.z;

		}
	}
	*/
#pragma endregion

	//モデル読み込み
	ModelData modelData = LoadObjFile("resources", "plane.obj");

	//頂点リソースを作る
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	//リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	//使用するリソースのサイズは頂点のサイズ
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());

	//1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	//頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;

	//書き込むためのアドレス取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	//頂点データをリソースにコピー
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData)* modelData.vertices.size());

	//Sprite用の頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	//リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

	//使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;

	//1頂点当たりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	//index用
	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};

	//リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();

	//使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;

	//インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	//インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;
	indexDataSprite[1] = 1;
	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 3;
	indexDataSprite[5] = 2;

	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	//左下
	vertexDataSprite[0].position = { 0.0f,360.0f,0.0f,1.0f };
	vertexDataSprite[0].texcoord = { 0.0f,1.0f };
	vertexDataSprite[0].normal = { 0.0f,0.0f,-1.0f };
	//左上
	vertexDataSprite[1].position = { 0.0f,0.0f,0.0f,1.0f };
	vertexDataSprite[1].texcoord = { 0.0f,0.0f };
	vertexDataSprite[1].normal = { 0.0f,0.0f,-1.0f };
	//右下
	vertexDataSprite[2].position = { 640.0f,360.0f,0.0f,1.0f };
	vertexDataSprite[2].texcoord = { 1.0f,1.0f };
	vertexDataSprite[2].normal = { 0.0f,0.0f,-1.0f };
	//右上
	vertexDataSprite[3].position = { 640.0f,0.0f,0.0f,1.0f };
	vertexDataSprite[3].texcoord = { 1.0f,0.0f };
	vertexDataSprite[3].normal = { 0.0f,0.0f,-1.0f };

	//マテリアル用のリソースを作る。今回はcolor１つ分のサイズを用意する
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));

	//マテリアルにデータを書き込む
	Material* materialData = nullptr;

	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	//色を書き込む
	*materialData = { Vector4(1.0f, 1.0f, 1.0f, 1.0f),true };
	materialData->uvTransform = Matrix::MakeIdentity4x4();

	//WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));

	//データを書き込む
	TransformationMatrix* wvpData = nullptr;

	//書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	//単位行列を書き込んでおく
	*wvpData = { Matrix::MakeIdentity4x4() ,Matrix::MakeIdentity4x4() };


	//平行光源用リソース
	ID3D12Resource* directionalLightResouce = CreateBufferResource(device, sizeof(DirectionalLight));
	//データを書き込む
	DirectionalLight* directionalLightData = nullptr;
	//アドレスの取得
	directionalLightResouce->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	//マテリアル用のリソースを作る。今回はcolor１つ分のサイズを用意する
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));

	//マテリアルにデータを書き込む
	Material* materialDataSprite = nullptr;

	//書き込むためのアドレスを取得
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	//色を書き込む
	*materialDataSprite = { Vector4(1.0f, 1.0f, 1.0f, 1.0f),false };
	materialDataSprite->uvTransform = Matrix::MakeIdentity4x4();

	//Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));

	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;

	//書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	//単位行列を書き込んで置く
	*transformationMatrixDataSprite = { Matrix::MakeIdentity4x4() ,Matrix::MakeIdentity4x4() };

	//DepthStencilTextureをウィンドウのサイズで作成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	//DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisidleはfalse
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	//DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

	//Format。基本的にはResourceに合わせる
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//2dTexture
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	//DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	//ビューポート
	D3D12_VIEWPORT viewport{};

	//クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	//シザー矩形
	D3D12_RECT scissorRect{};

	//基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	//Transform変数を作る
	Transform transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform transformSprite = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	DirectionalLight directionalLight = { {1.0f,1.0f,1.0f,1.0f},{0.0f,-1.0f,0.0f},1.0f };

	//UVTransform用
	Transform uvTransformSprite = {
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f},
	};

	//カメラを作る
	Transform cameraTransform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-10.0f} };

	//Textureを読んで転送する
	DirectX::ScratchImage mipImages[2];
	mipImages[0] = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages[0].GetMetadata();
	ID3D12Resource* textureResource[2];
	textureResource[0] = CreateTextureResource(device, metadata);
	ID3D12Resource* intermediateResource[2];
	intermediateResource[0] = UploadTextureData(textureResource[0], mipImages[0], device, commandList);

	mipImages[1] = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages[1].GetMetadata();
	textureResource[1] = CreateTextureResource(device, metadata2);
	intermediateResource[1] = UploadTextureData(textureResource[1], mipImages[1], device, commandList);

	//metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc[2]{};
	srvDesc[0].Format = metadata.format;
	srvDesc[0].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc[1].Format = metadata2.format;
	srvDesc[1].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	//2Dテクスチャ
	srvDesc[0].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc[0].Texture2D.MipLevels = UINT(metadata.mipLevels);

	srvDesc[1].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc[1].Texture2D.MipLevels = UINT(metadata2.mipLevels);

	//SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU[2];
	textureSrvHandleCPU[0] = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU[2];
	textureSrvHandleGPU[0] = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);

	textureSrvHandleCPU[1] = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
	textureSrvHandleGPU[1] = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);

	//SRVの作成
	device->CreateShaderResourceView(textureResource[0], &srvDesc[0], textureSrvHandleCPU[0]);

	device->CreateShaderResourceView(textureResource[1], &srvDesc[1], textureSrvHandleCPU[1]);

	//SRV切り替え用のフラグ
	bool useMonsterBall = true;

#ifdef USE_IMGUI
	//ImGuiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device,
		swapChainDesc.BufferCount,
		rtvDesc.Format, srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif // USE_IMGUI

	//メインループ
	MSG msg{};

	//ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		//Windowにメッセージが来ていたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			//ゲームの処理

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			//開発用UIの処理
			ImGui::ShowDemoWindow();

			ImGui::Begin("Window");
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
			ImGui::SliderFloat4("LightColor", &directionalLight.color.x, 0.0f, 1.0f);
			ImGui::SliderFloat3("LightDirection", &directionalLight.direction.x, -1.0f, 1.0f);
			ImGui::SliderFloat("LightIntensity", &directionalLight.intensity, 0.0f, 1.0f);
			ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
			ImGui::DragFloat3("rotate", &transform.rotate.x, 0.01f);
			ImGui::End();

#endif // USE_IMGUI

			//transform.rotate.y += 0.03f;
			Matrix4x4 worldMatrix = Matrix::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 cameraMatrix = Matrix::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Matrix::Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = Matrix::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 worldViewProjectionMatrix = Matrix::Multiply(worldMatrix, Matrix::Multiply(viewMatrix, projectionMatrix));

			wvpData->WVP = worldViewProjectionMatrix;
			wvpData->World = worldMatrix;
			*directionalLightData = directionalLight;

			//Sprute用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = Matrix::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = Matrix::MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = Matrix::MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Matrix::Multiply(worldMatrixSprite, Matrix::Multiply(viewMatrixSprite, projectionMatrixSprite));
			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
			transformationMatrixDataSprite->World = worldMatrixSprite;

			//UVTransform用の行列作成
			Matrix4x4 uvTransformMatrix = Matrix::MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Matrix::Multiply(uvTransformMatrix, Matrix::MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Matrix::Multiply(uvTransformMatrix, Matrix::MakeTranslateMatrix(uvTransformSprite.translate));
			materialDataSprite->uvTransform = uvTransformMatrix;

#ifdef USE_IMGUI
			//ImGuiの内部コマンドを生成する
			ImGui::Render();
#endif // USE_IMGUI

			//これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			//TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};

			//今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			//Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

			//バリアを張る対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex];

			//遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;

			//遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			//TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			//指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			//指定した色で画面全体をクリアする(RGBAの順)
			float clearColor[] = { 0.1f,0.25f,0.5f,1.0f };
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			//描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			//Viewportを設定
			commandList->RSSetViewports(1, &viewport);

			//Scirssorを設定
			commandList->RSSetScissorRects(1, &scissorRect);

			//RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature);

			//PSOを設定
			commandList->SetPipelineState(graphicsPipelineState);

			//VBVを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

			//形状を設定。PSOに設定している物とはまた別。同じものを設定すると考えておけばいい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			//wvp用のCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			//SRVのDescriptorTableの先頭を設定。2はrootParameter[2]である。
			if (useMonsterBall) {
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[1]);
			} else {
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[0]);
			}

			//平行光源用
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResouce->GetGPUVirtualAddress());

			//描画！(DrawCall/ドローコール)。３頂点で１つのインスタンス。インスタンスについては今後
			//commandList->DrawInstanced(kSubdivision * kSubdivision * 6, 1, 0, 0);

			commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);

			//Spriteの描画。変更の必要なものだけ変更する
			//VBVを設定する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			//IBAを設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite);

			//TransformationMatrixCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

			//Sprite用の画像
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[0]);

			//描画!(DrawCall/ドローコール)
			//commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

#ifdef USE_IMGUI
			//実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif // USE_IMGUI

			//今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

			//TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//コマンドリストの内容を確定させる。全てのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			//GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);

			//GPUとOSに画面の交換を行うように通知する
			swapChain->Present(1, 0);

			//Fenceの値を更新
			fenceValue++;

			//GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence, fenceValue);

			//Fenceの値が指定したSignal値にたどり着いているか確認する
			//GetCompletedValueの初期値はFence作成時に渡した初期値
			if (fence->GetCompletedValue() < fenceValue) {
				//指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				//イベント待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			//次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, nullptr);
			assert(SUCCEEDED(hr));
		}
	}

	//COMの終了処理
	CoUninitialize();

#ifdef USE_IMGUI
	//ImGuiの終了処理(初期化と逆の順番で行う)
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif // USE_IMGUI

	//解放処理
	CloseHandle(fenceEvent);
	fence->Release();
	rtvDescriptorHeap->Release();
	srvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();
#ifdef _DEBUG
	debugController->Release();
#endif // _DEBUG
	CloseWindow(hwnd);
	vertexResource->Release();
	graphicsPipelineState->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
	rootSignature->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
	materialResource->Release();
	wvpResource->Release();
	for (int i = 0;i < 2;i++) {
		mipImages[i].Release();
		textureResource[i]->Release();
		intermediateResource[i]->Release();
	}
	depthStencilResource->Release();
	dsvDescriptorHeap->Release();
	vertexResourceSprite->Release();
	materialResourceSprite->Release();
	transformationMatrixResourceSprite->Release();
	directionalLightResouce->Release();
	indexResourceSprite->Release();

	//リソースチェック
	IDXGIDebug1* debug;

	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}

	return 0;
}