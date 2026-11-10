#include<Windows.h>
#include<sal.h>
#include<cstdint>
#include<string>
#include<format>
#include<filesystem>
#include<fstream>
#include<chrono>
#include <iostream>

#include <d3d12.h>

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

#include<vector>

#define _USE_MATH_DEFINES
#include<math.h>

#include <sstream>


#include<xaudio2.h>
#pragma comment(lib,"xaudio2.lib")

#include"DebugCamera.h"
#include"Input.h"
#include"WinAPI.h"
#include"DirectXBasics.h"
#include"Logger.h"
#include"StringUtility.h"

using namespace StringUtility;
using namespace Logger;

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

struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		//リソースチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;

		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

//チャンクヘッダ
struct ChunkHeader {
	char id[4]; //チャンク毎のID
	int32_t size; //チャンクサイズ
};

//RIFFヘッダチャンク
struct RiffHeader {
	ChunkHeader chunk; //"RIFF"
	char type[4]; //"WAVE"
};

//FMTチャンク
struct FormatChunk {
	ChunkHeader chunk; //"fmt"
	WAVEFORMATEX fmt; //波形フォーマット
};

//音声データ
struct SoundData {
	WAVEFORMATEX wfex; //波形フォーマット
	BYTE* pBuffer; //バッファの先頭アドレス
	unsigned int bufferSize; //バッファのサイズ
};

//ブレンドモード
enum BlendMode {
	kBlendModeNone, // ブレンドなし
	kBlendModeNormal, // 通常αブレンド、デフォルト
	kBlendModeAdd, // 加算
	kBlendModeSubtract, // 減算
	kBlendModeMultily, // 乗算
	kBlendModeScreen, // スクリーン

	kCountOfBlendMode, // 使用禁止 (要素数カウント用)
};

const char* kBlendModeNames[] = {
	"None", // ブレンドなし
	"Normal", // 通常αブレンド、デフォルト
	"Add", // 加算
	"Subtract", // 減算
	"Multily", // 乗算
	"Screen", // スクリーン
};


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







//Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height) {
//
//
//	return resource;
//}


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

//音声データの読み込み
SoundData SoundLoadWave(const char* filename) {
	//1.　ファイルオープン

	//ファイル入力ストリームのインスタンス
	std::ifstream file;

	//.wavファイルをバイナリモードで開く
	file.open(filename, std::ios_base::binary);

	//ファイルオープン失敗を検出する
	assert(file.is_open());

	//2.　.wavデータ読み込み

	//RIFFヘッダーの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));

	//ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
		assert(0);
	}

	//タイプがWAVEかチェック
	if (strncmp(riff.type, "WAVE", 4) != 0) {
		assert(0);
	}

	//Formatチャンクの読み込み
	FormatChunk format = {};

	//チャンクヘッダーの確認
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
		assert(0);
	}

	//チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size);

	//Dataチャンクの読み込み
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));

	//JUNKチャンクを検出した場合
	if (strncmp(data.id, "JUNK", 4) == 0) {
		//読み取り位置をJUNKチャンクの終わりまで進める
		file.seekg(data.size, std::ios_base::cur);

		//再読み込み
		file.read((char*)&data, sizeof(data));
	}

	if (strncmp(data.id, "data", 4) != 0) {
		assert(0);
	}

	//Dataチャンクのデータ部(波形データ)の読み込み
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	//3.　ファイルクローズ
	file.close();

	//4.　読み込んだ音声データをreturn

	//returnするための音声データ
	SoundData soundData = {};

	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;

	return soundData;
}

//音声データ開放
void SoundUnload(SoundData* soundData) {
	//バッファのメモリを解放
	delete[] soundData->pBuffer;

	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

//音声再生
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {
	HRESULT hr;

	//波形フォーマットを元にSourceVoiceの生成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	hr = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));

	//再生する波形データの設定
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;

	//波形データの再生
	hr = pSourceVoice->SubmitSourceBuffer(&buf);
	hr = pSourceVoice->Start();
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> BlendModeUpdate(ID3D12Device* device, D3D12_BLEND_DESC& blendDesc, D3D12_GRAPHICS_PIPELINE_STATE_DESC& graphicsPipelineStateDesc, BlendMode& blendMode) {
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	if (blendMode == kBlendModeNone) {
		blendDesc.RenderTarget[0].BlendEnable = false;
	} else {
		blendDesc.RenderTarget[0].BlendEnable = true;
	}

	switch (blendMode) {
	case kBlendModeNone:
		break;
	case kBlendModeNormal:
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;
	case kBlendModeAdd:
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeSubtract:
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeMultily:
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
		break;
	case kBlendModeScreen:
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;
	default:
		break;
	}

	if (blendMode != kBlendModeNone) {
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	}

	graphicsPipelineStateDesc.BlendState = blendDesc;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;

	HRESULT hr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));

	return graphicsPipelineState;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	D3DResourceLeakChecker leakCheck;

	//ポインタ
	WinAPI* winApi = nullptr;
	DirectXBasics* directXBasics = nullptr;

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

	/*HRESULTはWindows系のエラーコードであり、
	関数が成功したかどうかをSUCCEEDEDマクロで判定できる*/
	HRESULT hr;


	//WindowsAPIの初期化
	winApi = new WinAPI();
	winApi->Initialize();

	//ログ記録
	Log("Window Create!\n");

	//DirectXの初期化
	directXBasics = new DirectXBasics();
	directXBasics->Initialize(winApi);




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
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = directXBasics->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
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

	//BlendStateの設定(ブレンドモードの設定)
	D3D12_BLEND_DESC blendDesc{};

	//全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	//RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	//裏側(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	//Shaderをコンパイルする
	IDxcBlob* vertexShaderBlob = directXBasics->CompileShader(L"Object3D.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);
	
	IDxcBlob* pixelShaderBlob = directXBasics->CompileShader(L"Object3D.PS.hlsl", L"ps_6_0");
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
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();

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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = directXBasics->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

#pragma region 球の頂点データ
	/*
	//球の分割数
	const uint32_t kSubdivision = 16;

	//頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * 6 * kSubdivision * kSubdivision);

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
	ModelData modelData = LoadObjFile("resources", "fence.obj");

	//頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource =directXBasics->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

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
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	//Sprite用の頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = directXBasics->CreateBufferResource( sizeof(VertexData) * 4);

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	//リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

	//使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;

	//1頂点当たりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	//index用
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = directXBasics->CreateBufferResource( sizeof(uint32_t) * 6);

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
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = directXBasics->CreateBufferResource( sizeof(Material));

	//マテリアルにデータを書き込む
	Material* materialData = nullptr;

	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	//色を書き込む
	*materialData = { Vector4(1.0f, 1.0f, 1.0f, 1.0f),true };
	materialData->uvTransform = Matrix::MakeIdentity4x4();

	//WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = directXBasics->CreateBufferResource( sizeof(TransformationMatrix));

	//データを書き込む
	TransformationMatrix* wvpData = nullptr;

	//書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	//単位行列を書き込んでおく
	*wvpData = { Matrix::MakeIdentity4x4() ,Matrix::MakeIdentity4x4() };


	//平行光源用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResouce = directXBasics->CreateBufferResource( sizeof(DirectionalLight));
	//データを書き込む
	DirectionalLight* directionalLightData = nullptr;
	//アドレスの取得
	directionalLightResouce->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	//マテリアル用のリソースを作る。今回はcolor１つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = directXBasics->CreateBufferResource( sizeof(Material));

	//マテリアルにデータを書き込む
	Material* materialDataSprite = nullptr;

	//書き込むためのアドレスを取得
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	//色を書き込む
	*materialDataSprite = { Vector4(1.0f, 1.0f, 1.0f, 1.0f),false };
	materialDataSprite->uvTransform = Matrix::MakeIdentity4x4();

	//Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = directXBasics->CreateBufferResource(sizeof(TransformationMatrix));

	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;

	//書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	//単位行列を書き込んで置く
	*transformationMatrixDataSprite = { Matrix::MakeIdentity4x4() ,Matrix::MakeIdentity4x4() };




	

	

	
	
	//ポインタ
	Input* input = nullptr;

	//入力の初期化
	input = new Input();
	input->Initialize(winApi);

	//XAudio2の変数宣言
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;

	//XAudioエンジンのインスタンスを作成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);

	//マスターボイスを生成
	hr = xAudio2->CreateMasteringVoice(&masterVoice);

	//Transform変数を作る
	Transform transform = { { 1.0f,1.0f,1.0f },{ 0.0f,0.0f,0.0f },{ 0.0f,0.0f,0.0f } };
	Transform transformSprite = { { 1.0f,1.0f,1.0f },{ 0.0f,0.0f,0.0f },{ 0.0f,0.0f,0.0f } };

	DirectionalLight directionalLight = { { 1.0f,1.0f,1.0f,1.0f },{ 0.0f,-1.0f,0.0f },1.0f };

	//UVTransform用
	Transform uvTransformSprite = {
		{ 1.0f,1.0f,1.0f },
		{ 0.0f,0.0f,0.0f },
		{ 0.0f,0.0f,0.0f },
	};

	//カメラを作る
	Transform cameraTransform = { { 1.0f,1.0f,1.0f },{ 0.0f,0.0f,0.0f },{ 0.0f,0.0f,-10.0f } };

	DebugCamera debugCamera;
	debugCamera.Initialize();

	//Textureを読んで転送する
	DirectX::ScratchImage mipImages[2];
	mipImages[0] = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages[0].GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource[2];
	textureResource[0] =directXBasics->CreateTextureResource( metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource[2];
	intermediateResource[0] =directXBasics->UploadTextureData(textureResource[0], mipImages[0]);

	mipImages[1] = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages[1].GetMetadata();
	textureResource[1] = directXBasics->CreateTextureResource( metadata2);
	intermediateResource[1] = directXBasics->UploadTextureData(textureResource[1], mipImages[1]);

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
	textureSrvHandleCPU[0] = directXBasics->GetSRVCPUDescriptorHandle(1);
		//GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU[2];
	textureSrvHandleGPU[0] = directXBasics->GetSRVGPUDescriptorHandle(1);
		//GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);

	textureSrvHandleCPU[1] = directXBasics->GetSRVCPUDescriptorHandle(2);
		//GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
	textureSrvHandleGPU[1] = directXBasics->GetSRVGPUDescriptorHandle(2);
		//GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);

	//SRVの作成
	directXBasics->GetDevice()->CreateShaderResourceView(textureResource[0].Get(), &srvDesc[0], textureSrvHandleCPU[0]);

	directXBasics->GetDevice()->CreateShaderResourceView(textureResource[1].Get(), &srvDesc[1], textureSrvHandleCPU[1]);

	//SRV切り替え用のフラグ
	bool useMonsterBall = true;

	//音声読み込み
	SoundData soundData[] = { SoundLoadWave("resources/Alarm01.wav") };

	//音声再生フラグ
	bool isSoundPlay = false;

	//デバックカメラ使用フラグ
	bool useDebugCamera = false;

	BlendMode blendMode = BlendMode::kBlendModeNormal;
	int mode = static_cast<int>(blendMode);
	BlendMode preBlendMode = blendMode;


	//ゲームループ
	while (true) {
		//Windowsのメッセージ処理
		if (winApi->ProcessMessage()) {
			//ゲームループを抜ける
			break;
		}
		//ゲームの処理

		//入力の更新
		input->Update();

#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		//開発用UIの処理
		ImGui::ShowDemoWindow();

		ImGui::Begin("Window");
		ImGui::Button("SoundPlay");
		if (ImGui::IsItemActive()) {
			isSoundPlay = true;
		}
		ImGui::Checkbox("useMonsterBall", &useMonsterBall);
		ImGui::SliderFloat4("LightColor", &directionalLight.color.x, 0.0f, 1.0f);
		ImGui::SliderFloat3("LightDirection", &directionalLight.direction.x, -1.0f, 1.0f);
		ImGui::SliderFloat("LightIntensity", &directionalLight.intensity, 0.0f, 1.0f);
		ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
		ImGui::DragFloat3("rotate", &transform.rotate.x, 0.01f);
		ImGui::ColorEdit4("Color", &materialData->color.x);
		ImGui::Combo("BlendMode", &mode, kBlendModeNames, IM_ARRAYSIZE(kBlendModeNames));
		ImGui::End();

#endif // USE_IMGUI
		blendMode = static_cast<BlendMode>(mode);

		if (preBlendMode != blendMode) {
			graphicsPipelineState = BlendModeUpdate(directXBasics->GetDevice(), blendDesc, graphicsPipelineStateDesc, blendMode);
			hr = directXBasics->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
			preBlendMode = blendMode;
		}


		if (isSoundPlay) {
			//音声再生
			SoundPlayWave(xAudio2.Get(), soundData[0]);
			isSoundPlay = false;
		}

		if (input->TriggerKey(DIK_SPACE)) {
			if (useDebugCamera) {
				useDebugCamera = false;
			} else {
				useDebugCamera = true;
			}
		}
		Matrix4x4 viewMatrix;


		Matrix4x4 worldMatrix = Matrix::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		Matrix4x4 cameraMatrix = Matrix::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
		if (useDebugCamera) {
			debugCamera.Update(*input);
			viewMatrix = debugCamera.GetViewMatrix();
		} else {
			viewMatrix = Matrix::Inverse(cameraMatrix);
		}
		Matrix4x4 projectionMatrix = Matrix::MakePerspectiveFovMatrix(0.45f, float(WinAPI::kClientWidth) / float(WinAPI::kClientHeight), 0.1f, 100.0f);
		Matrix4x4 worldViewProjectionMatrix = Matrix::Multiply(worldMatrix, Matrix::Multiply(viewMatrix, projectionMatrix));

		wvpData->WVP = worldViewProjectionMatrix;
		wvpData->World = worldMatrix;
		*directionalLightData = directionalLight;

		//Sprute用のWorldViewProjectionMatrixを作る
		Matrix4x4 worldMatrixSprite = Matrix::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
		Matrix4x4 viewMatrixSprite = Matrix::MakeIdentity4x4();
		Matrix4x4 projectionMatrixSprite = Matrix::MakeOrthographicMatrix(0.0f, 0.0f, float(WinAPI::kClientWidth), float(WinAPI::kClientHeight), 0.0f, 100.0f);
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

		directXBasics->PreDraw();
		
		//RootSignatureを設定。PSOに設定しているけど別途設定が必要
		directXBasics->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());

		//PSOを設定
		directXBasics->GetCommandList()->SetPipelineState(graphicsPipelineState.Get());

		//VBVを設定
		directXBasics->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);

		//形状を設定。PSOに設定している物とはまた別。同じものを設定すると考えておけばいい
		directXBasics->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//マテリアルCBufferの場所を設定
		directXBasics->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

		//wvp用のCBufferの場所を設定
		directXBasics->GetCommandList()->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

		//SRVのDescriptorTableの先頭を設定。2はrootParameter[2]である。
		if (useMonsterBall) {
			directXBasics->GetCommandList()->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[1]);
		} else {
			directXBasics->GetCommandList()->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[0]);
		}

		//平行光源用
		directXBasics->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResouce->GetGPUVirtualAddress());

		//描画！(DrawCall/ドローコール)。３頂点で１つのインスタンス。インスタンスについては今後
		//directXBasics->GetCommandList()->DrawInstanced(kSubdivision * kSubdivision * 6, 1, 0, 0);

		directXBasics->GetCommandList()->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);

		//Spriteの描画。変更の必要なものだけ変更する
		//VBVを設定する
		directXBasics->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
		//IBAを設定
		directXBasics->GetCommandList()->IASetIndexBuffer(&indexBufferViewSprite);

		//TransformationMatrixCBufferの場所を設定
		directXBasics->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

		//マテリアルCBufferの場所を設定
		directXBasics->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

		//Sprite用の画像
		directXBasics->GetCommandList()->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[0]);

		//描画!(DrawCall/ドローコール)
		//directXBasics->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);

#ifdef USE_IMGUI
			//実際のcommandListのImGuiの描画コマンドを積む
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), directXBasics->GetCommandList());
#endif // USE_IMGUI

		directXBasics->PostDraw();
	}




#ifdef USE_IMGUI
	//ImGuiの終了処理(初期化と逆の順番で行う)
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif // USE_IMGUI

	//解放処理
	winApi->Finalize();

	//XAudio2解放
	xAudio2.Reset();
	//音声データ解放
	SoundUnload(&soundData[0]);

	//入力解放
	delete input;
	//WindowAPI解放
	delete winApi;
	//DirectX解放
	delete directXBasics;

	return 0;
}