// InferenceCore.cpp — AI/ML inference engine implementation
// Backend: ONNX Runtime + TensorRT (optional) + CUDA

#include "InferenceCore.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <onnxruntime_cxx_api.h>

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <chrono>
#include <vector>
#include <atomic>

// WinInet for model key download
#include <wininet.h>
#pragma comment(lib, "wininet.lib")

// ── Internal types ────────────────────────────────────────────────────────────

struct ModelSession {
    std::string name;
    Ort::Session* ortSession = nullptr;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    float lastInferenceMs = 0.f;
    std::atomic<int> frameCount{0};
    std::chrono::steady_clock::time_point startTime;
    int64_t inputW = 0, inputH = 0;
    bool loaded = false;
};

struct InferenceCoreState {
    std::string sessionToken;
    std::string discordId;
    bool authenticated = false;

    Ort::Env* ortEnv = nullptr;
    Ort::SessionOptions sessionOptions;
    int deviceIdx = 0;

    std::unordered_map<std::string, ModelSession> models;
    std::mutex modelsMutex;

    // Video ring buffer SHM
    HANDLE hVideoMap  = nullptr;
    HANDLE hVideoEvent = nullptr;
    void*  pVideoView = nullptr;

    // Result buffer SHM
    HANDLE hResultMap = nullptr;
    void*  pResultView = nullptr;

    InfCoreLogCallback logCb = nullptr;
    void* logUser = nullptr;
    int logLevel = 1;
};

static InferenceCoreState* g_state = nullptr;

static void Log(int level, const char* fmt, ...)
{
    if (!g_state || !g_state->logCb) return;
    if (level < g_state->logLevel) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_state->logCb(level, buf, g_state->logUser);
}

// ── Model key fetch ───────────────────────────────────────────────────────────

static std::vector<uint8_t> fetchModelKey(const std::string& modelName)
{
    // POST https://helios.inputsense.com/api/scripts/onnx/get-key-v2
    // Headers:
    //   Authorization: Bearer <sessionToken>
    //   X-Helios-API-Version: 4
    //   User-Agent: Helios/2.0
    //   Origin: https://www.inputsense.com
    //   Referer: https://www.inputsense.com/
    // Body: {"model": "<modelName>", "hwid": "<hwid>"}

    HINTERNET hInet = InternetOpenA("Helios/2.0",
                                     INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return {};

    HINTERNET hConn = InternetConnectA(hInet, "helios.inputsense.com",
                                        INTERNET_DEFAULT_HTTPS_PORT,
                                        nullptr, nullptr,
                                        INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return {}; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST",
                                       "/api/scripts/onnx/get-key-v2",
                                       nullptr, nullptr, nullptr,
                                       INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return {}; }

    std::string authHeader = "Authorization: Bearer " + g_state->sessionToken;
    HttpAddRequestHeadersA(hReq, authHeader.c_str(), (DWORD)-1,
                            HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    HttpAddRequestHeadersA(hReq, "X-Helios-API-Version: 4", (DWORD)-1,
                            HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    HttpAddRequestHeadersA(hReq, "Content-Type: application/json", (DWORD)-1,
                            HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    HttpAddRequestHeadersA(hReq, "Origin: https://www.inputsense.com", (DWORD)-1,
                            HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);

    std::string body = "{\"model\":\"" + modelName + "\",\"discord_id\":\"" +
                       g_state->discordId + "\"}";
    HttpSendRequestA(hReq, nullptr, 0,
                     const_cast<char*>(body.c_str()), static_cast<DWORD>(body.size()));

    std::vector<uint8_t> response;
    char buf[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        response.insert(response.end(), buf, buf + bytesRead);
    }

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);

    // Response JSON: {"key": "<base64-encoded AES-GCM key>"}
    // Parse manually (no JSON lib in InferenceCore to minimize deps)
    std::string resp(response.begin(), response.end());
    auto keyStart = resp.find("\"key\":\"");
    if (keyStart == std::string::npos) return {};
    keyStart += 7;
    auto keyEnd = resp.find("\"", keyStart);
    if (keyEnd == std::string::npos) return {};

    std::string keyB64 = resp.substr(keyStart, keyEnd - keyStart);

    // Base64 decode
    DWORD decodedLen = 0;
    CryptStringToBinaryA(keyB64.c_str(), 0, CRYPT_STRING_BASE64,
                          nullptr, &decodedLen, nullptr, nullptr);
    std::vector<uint8_t> key(decodedLen);
    CryptStringToBinaryA(keyB64.c_str(), 0, CRYPT_STRING_BASE64,
                          key.data(), &decodedLen, nullptr, nullptr);
    return key;
}

// ── AES-GCM model decryption ──────────────────────────────────────────────────

static std::vector<uint8_t> decryptModel(const uint8_t* encrypted, size_t encLen,
                                          const uint8_t* key, size_t keyLen)
{
    if (encLen < 28) return {};

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

    struct { BCRYPT_KEY_DATA_BLOB_HEADER hdr; uint8_t data[32]; } blob = {};
    blob.hdr.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
    blob.hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    blob.hdr.cbKeyData = 32;
    memcpy(blob.data, key, std::min(keyLen, (size_t)32));

    BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                    &hKey, nullptr, 0,
                    reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0);

    // Layout: nonce(12) | ciphertext | tag(16)
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo = {};
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(encrypted);
    authInfo.cbNonce = 12;
    authInfo.pbTag   = const_cast<PUCHAR>(encrypted + encLen - 16);
    authInfo.cbTag   = 16;

    size_t plainLen = encLen - 28;
    std::vector<uint8_t> plain(plainLen);
    ULONG actualLen = 0;
    NTSTATUS status = BCryptDecrypt(hKey,
                                     const_cast<PUCHAR>(encrypted + 12),
                                     static_cast<ULONG>(plainLen),
                                     &authInfo, nullptr, 0,
                                     plain.data(), static_cast<ULONG>(plainLen),
                                     &actualLen, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) return {};
    plain.resize(actualLen);
    return plain;
}

// ── Public API ────────────────────────────────────────────────────────────────

INFCORE_API int infcore_init()
{
    if (g_state) return 0;
    g_state = new InferenceCoreState;

    try {
        g_state->ortEnv = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "InferenceCore");
        g_state->sessionOptions.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        g_state->sessionOptions.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
    } catch (...) {
        delete g_state;
        g_state = nullptr;
        return -1;
    }

    Log(1, "InferenceCore initialized");
    return 0;
}

INFCORE_API void infcore_shutdown()
{
    if (!g_state) return;

    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    for (auto& [name, sess] : g_state->models) {
        delete sess.ortSession;
    }
    g_state->models.clear();

    delete g_state->ortEnv;
    delete g_state;
    g_state = nullptr;
}

INFCORE_API int infcore_set_session(const char* token, const char* discordId)
{
    if (!g_state) return -1;
    g_state->sessionToken  = token ? token : "";
    g_state->discordId     = discordId ? discordId : "";
    g_state->authenticated = !g_state->sessionToken.empty();
    Log(1, "Session set for Discord ID: %s", g_state->discordId.c_str());
    return 0;
}

INFCORE_API int infcore_validate_session()
{
    return (g_state && g_state->authenticated) ? 1 : 0;
}

INFCORE_API int infcore_load_model(const char* modelName, int backend)
{
    if (!g_state || !modelName) return -1;
    if (!g_state->authenticated) {
        Log(3, "infcore_load_model: not authenticated");
        return -2;
    }

    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    if (g_state->models.count(modelName) && g_state->models[modelName].loaded)
        return 0; // already loaded

    // Fetch decryption key from server
    auto key = fetchModelKey(modelName);
    if (key.empty()) {
        Log(3, "infcore_load_model: failed to fetch key for %s", modelName);
        return -3;
    }

    // Find encrypted model file: %APPDATA%\HeliosProject\Helios\models\<name>.enc
    char appDataPath[MAX_PATH];
    SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath);
    std::string modelPath = std::string(appDataPath) +
                            "\\HeliosProject\\Helios\\models\\" +
                            modelName + ".enc";

    HANDLE hFile = CreateFileA(modelPath.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log(3, "infcore_load_model: model file not found: %s", modelPath.c_str());
        return -4;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    std::vector<uint8_t> encrypted(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, encrypted.data(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    // Decrypt
    auto plain = decryptModel(encrypted.data(), encrypted.size(), key.data(), key.size());
    if (plain.empty()) {
        Log(3, "infcore_load_model: decryption failed for %s", modelName);
        return -5;
    }

    // Load ONNX model from memory
    ModelSession& sess = g_state->models[modelName];
    sess.name = modelName;

    try {
        Ort::SessionOptions opts = g_state->sessionOptions;

        if (backend == 1) {
            // TensorRT execution provider
            OrtTensorRTProviderOptions trtOpts = {};
            trtOpts.device_id = g_state->deviceIdx;
            opts.AppendExecutionProvider_TensorRT(trtOpts);
        }

        // CUDA execution provider
        OrtCUDAProviderOptions cudaOpts = {};
        cudaOpts.device_id = g_state->deviceIdx;
        opts.AppendExecutionProvider_CUDA(cudaOpts);

        sess.ortSession = new Ort::Session(*g_state->ortEnv,
                                            plain.data(), plain.size(), opts);

        // Cache input/output names
        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputs  = sess.ortSession->GetInputCount();
        size_t numOutputs = sess.ortSession->GetOutputCount();

        for (size_t i = 0; i < numInputs; ++i) {
            auto name = sess.ortSession->GetInputNameAllocated(i, allocator);
            sess.inputNames.push_back(name.get());
        }
        for (size_t i = 0; i < numOutputs; ++i) {
            auto name = sess.ortSession->GetOutputNameAllocated(i, allocator);
            sess.outputNames.push_back(name.get());
        }

        // Get input shape
        auto inputInfo  = sess.ortSession->GetInputTypeInfo(0);
        auto shapeInfo  = inputInfo.GetTensorTypeAndShapeInfo();
        auto shape      = shapeInfo.GetShape();
        if (shape.size() >= 4) {
            sess.inputH = shape[2];
            sess.inputW = shape[3];
        }

        sess.loaded    = true;
        sess.startTime = std::chrono::steady_clock::now();

        Log(1, "infcore_load_model: loaded '%s' (inputs=%zu outputs=%zu "
               "input_size=%lldx%lld)",
               modelName, numInputs, numOutputs, sess.inputW, sess.inputH);
        return 0;

    } catch (const Ort::Exception& e) {
        Log(3, "infcore_load_model: ORT error: %s", e.what());
        sess.loaded = false;
        return -6;
    }
}

INFCORE_API void infcore_unload_model(const char* modelName)
{
    if (!g_state || !modelName) return;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    auto it = g_state->models.find(modelName);
    if (it != g_state->models.end()) {
        delete it->second.ortSession;
        g_state->models.erase(it);
    }
}

INFCORE_API int infcore_model_is_loaded(const char* modelName)
{
    if (!g_state || !modelName) return 0;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    auto it = g_state->models.find(modelName);
    return (it != g_state->models.end() && it->second.loaded) ? 1 : 0;
}

INFCORE_API int infcore_get_model_count()
{
    if (!g_state) return 0;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    return static_cast<int>(g_state->models.size());
}

INFCORE_API void infcore_get_model_name(int index, char* buf, int bufLen)
{
    if (!g_state || !buf) return;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    int i = 0;
    for (const auto& [name, _] : g_state->models) {
        if (i++ == index) {
            strncpy_s(buf, bufLen, name.c_str(), _TRUNCATE);
            return;
        }
    }
}

INFCORE_API int infcore_run_inference(
    const char* modelName,
    const uint8_t* frameData,
    int width, int height, int stride,
    float* outputBuffer, int outputLen)
{
    if (!g_state || !modelName || !frameData || !outputBuffer) return -1;

    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    auto it = g_state->models.find(modelName);
    if (it == g_state->models.end() || !it->second.loaded) return -2;

    ModelSession& sess = it->second;
    auto t0 = std::chrono::steady_clock::now();

    try {
        // Resize/normalize frame to model input size
        int64_t inputW = sess.inputW > 0 ? sess.inputW : width;
        int64_t inputH = sess.inputH > 0 ? sess.inputH : height;

        // Input tensor: NCHW float32, normalized [0,1]
        std::vector<float> inputData(1 * 3 * inputH * inputW);
        // Simple bilinear resize + BGR→RGB + normalize
        for (int64_t y = 0; y < inputH; ++y) {
            for (int64_t x = 0; x < inputW; ++x) {
                int srcX = static_cast<int>(x * width  / inputW);
                int srcY = static_cast<int>(y * height / inputH);
                srcX = std::min(srcX, width  - 1);
                srcY = std::min(srcY, height - 1);
                const uint8_t* px = frameData + srcY * stride + srcX * 3;
                size_t idx = y * inputW + x;
                inputData[0 * inputH * inputW + idx] = px[2] / 255.f; // R
                inputData[1 * inputH * inputW + idx] = px[1] / 255.f; // G
                inputData[2 * inputH * inputW + idx] = px[0] / 255.f; // B
            }
        }

        std::vector<int64_t> inputShape = {1, 3, inputH, inputW};
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, inputData.data(), inputData.size(),
            inputShape.data(), inputShape.size());

        std::vector<const char*> inNames, outNames;
        for (const auto& n : sess.inputNames)  inNames.push_back(n.c_str());
        for (const auto& n : sess.outputNames) outNames.push_back(n.c_str());

        auto outputs = sess.ortSession->Run(
            Ort::RunOptions{nullptr},
            inNames.data(), &inputTensor, 1,
            outNames.data(), outNames.size());

        // Copy first output to caller's buffer
        if (!outputs.empty()) {
            auto& out    = outputs[0];
            auto* data   = out.GetTensorData<float>();
            auto   info  = out.GetTensorTypeAndShapeInfo();
            size_t count = info.GetElementCount();
            size_t copy  = std::min(count, (size_t)outputLen);
            memcpy(outputBuffer, data, copy * sizeof(float));
        }

        auto t1 = std::chrono::steady_clock::now();
        sess.lastInferenceMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        sess.frameCount++;
        return 0;

    } catch (const Ort::Exception& e) {
        Log(3, "infcore_run_inference ORT error: %s", e.what());
        return -3;
    }
}

INFCORE_API int infcore_run_pose(
    const uint8_t* frameData, int width, int height, int stride,
    float* keypointsBuf)
{
    // Uses the built-in "skeleton" pose model
    return infcore_run_inference("skeleton", frameData, width, height, stride,
                                  keypointsBuf, 51);
}

INFCORE_API int infcore_run_detect(
    const char* modelName,
    const uint8_t* frameData, int width, int height, int stride,
    float* boxesBuf, int maxBoxes)
{
    std::vector<float> raw(maxBoxes * 6);
    int ret = infcore_run_inference(modelName, frameData, width, height, stride,
                                     raw.data(), static_cast<int>(raw.size()));
    if (ret == 0 && boxesBuf)
        memcpy(boxesBuf, raw.data(), maxBoxes * 6 * sizeof(float));
    return ret;
}

INFCORE_API int infcore_run_inference_async(
    const char* modelName,
    const uint8_t* frameData,
    int width, int height, int stride)
{
    // Stub: sync for now; async via thread pool in production
    Q_UNUSED(modelName); Q_UNUSED(frameData);
    Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(stride);
    return 0;
}

INFCORE_API int infcore_poll_result(const char* modelName,
                                     float* outputBuffer, int outputLen)
{
    Q_UNUSED(modelName); Q_UNUSED(outputBuffer); Q_UNUSED(outputLen);
    return 0;
}

INFCORE_API int infcore_get_device_count()
{
    int count = 0;
    // cudaGetDeviceCount(&count);
    return count;
}

INFCORE_API void infcore_get_device_name(int deviceIdx, char* buf, int bufLen)
{
    if (!buf) return;
    strncpy_s(buf, bufLen, "CPU", _TRUNCATE);
}

INFCORE_API void infcore_set_device(int deviceIdx)
{
    if (g_state) g_state->deviceIdx = deviceIdx;
}

INFCORE_API int infcore_get_current_device()
{
    return g_state ? g_state->deviceIdx : 0;
}

INFCORE_API void infcore_get_device_memory(int, size_t* freeMb, size_t* totalMb)
{
    if (freeMb)  *freeMb  = 0;
    if (totalMb) *totalMb = 0;
}

INFCORE_API float infcore_get_last_inference_ms(const char* modelName)
{
    if (!g_state || !modelName) return 0.f;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    auto it = g_state->models.find(modelName);
    return (it != g_state->models.end()) ? it->second.lastInferenceMs : 0.f;
}

INFCORE_API float infcore_get_fps(const char* modelName)
{
    if (!g_state || !modelName) return 0.f;
    std::lock_guard<std::mutex> lock(g_state->modelsMutex);
    auto it = g_state->models.find(modelName);
    if (it == g_state->models.end()) return 0.f;
    auto elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - it->second.startTime).count();
    return elapsed > 0.f ? it->second.frameCount.load() / elapsed : 0.f;
}

INFCORE_API int infcore_attach_video_buffer(const char* shmName)
{
    if (!g_state || !shmName) return -1;
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, shmName);
    if (!hMap) return -2;
    g_state->hVideoMap  = hMap;
    g_state->pVideoView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    return 0;
}

INFCORE_API void infcore_detach_video_buffer()
{
    if (!g_state) return;
    if (g_state->pVideoView) { UnmapViewOfFile(g_state->pVideoView); g_state->pVideoView = nullptr; }
    if (g_state->hVideoMap)  { CloseHandle(g_state->hVideoMap);      g_state->hVideoMap  = nullptr; }
}

INFCORE_API int infcore_attach_result_buffer(const char* shmName, size_t bufferSize)
{
    if (!g_state || !shmName) return -1;
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, 0,
                                      static_cast<DWORD>(bufferSize), shmName);
    if (!hMap) return -2;
    g_state->hResultMap  = hMap;
    g_state->pResultView = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, bufferSize);
    return 0;
}

INFCORE_API void infcore_detach_result_buffer()
{
    if (!g_state) return;
    if (g_state->pResultView) { UnmapViewOfFile(g_state->pResultView); g_state->pResultView = nullptr; }
    if (g_state->hResultMap)  { CloseHandle(g_state->hResultMap);       g_state->hResultMap  = nullptr; }
}

INFCORE_API void infcore_set_log_callback(InfCoreLogCallback cb, void* user)
{
    if (g_state) { g_state->logCb = cb; g_state->logUser = user; }
}

INFCORE_API void infcore_set_log_level(int level)
{
    if (g_state) g_state->logLevel = level;
}
