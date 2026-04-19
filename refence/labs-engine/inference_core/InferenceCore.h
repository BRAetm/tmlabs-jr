#pragma once
// InferenceCore.h — AI/ML inference engine (ONNX + TensorRT + CUDA)
// C export API — called by Python (ctypes) and C++ plugins

#ifdef INFERENCECORE_EXPORTS
#define INFCORE_API extern "C" __declspec(dllexport)
#else
#define INFCORE_API extern "C" __declspec(dllimport)
#endif

#include <cstdint>
#include <cstddef>

// ── Session management ────────────────────────────────────────────────────────
INFCORE_API int  infcore_init();
INFCORE_API void infcore_shutdown();
INFCORE_API int  infcore_set_session(const char* token, const char* discordId);
INFCORE_API int  infcore_validate_session();

// ── Model management ──────────────────────────────────────────────────────────
// Models are AES-GCM encrypted; decryption key fetched from server
// Endpoint: POST https://helios.inputsense.com/api/scripts/onnx/get-key-v2
INFCORE_API int  infcore_load_model(const char* modelName, int backend); // 0=ONNX 1=TensorRT
INFCORE_API void infcore_unload_model(const char* modelName);
INFCORE_API int  infcore_model_is_loaded(const char* modelName);
INFCORE_API int  infcore_get_model_count();
INFCORE_API void infcore_get_model_name(int index, char* buf, int bufLen);

// ── Inference ─────────────────────────────────────────────────────────────────
// Input: BGR24 frame (width × height × 3 bytes)
// Output: detection results (model-dependent format)
INFCORE_API int  infcore_run_inference(
    const char* modelName,
    const uint8_t* frameData,
    int width, int height, int stride,
    float* outputBuffer, int outputLen
);

// Async inference (non-blocking)
INFCORE_API int  infcore_run_inference_async(
    const char* modelName,
    const uint8_t* frameData,
    int width, int height, int stride
);
INFCORE_API int  infcore_poll_result(const char* modelName,
                                     float* outputBuffer, int outputLen);

// ── Keypoint detection (skeleton model) ──────────────────────────────────────
// Returns 17 keypoints: x,y,confidence per point = 51 floats
// Keypoint order: nose, eyes, ears, shoulders, elbows, wrists,
//                 hips, knees, ankles
INFCORE_API int  infcore_run_pose(
    const uint8_t* frameData, int width, int height, int stride,
    float* keypointsBuf  // [17 × 3] = 51 floats
);

// ── Object detection ─────────────────────────────────────────────────────────
// Returns bounding boxes: x1,y1,x2,y2,confidence,classId per detection
INFCORE_API int  infcore_run_detect(
    const char* modelName,
    const uint8_t* frameData, int width, int height, int stride,
    float* boxesBuf, int maxBoxes  // [maxBoxes × 6]
);

// ── GPU management ────────────────────────────────────────────────────────────
INFCORE_API int  infcore_get_device_count();
INFCORE_API void infcore_get_device_name(int deviceIdx, char* buf, int bufLen);
INFCORE_API void infcore_set_device(int deviceIdx);
INFCORE_API int  infcore_get_current_device();
INFCORE_API void infcore_get_device_memory(int deviceIdx,
                                            size_t* freeMb, size_t* totalMb);

// ── Performance counters ──────────────────────────────────────────────────────
INFCORE_API float infcore_get_last_inference_ms(const char* modelName);
INFCORE_API float infcore_get_fps(const char* modelName);

// ── Shared memory integration ─────────────────────────────────────────────────
// Reads frames from Helios video ring buffer, writes results to inference SHM
INFCORE_API int  infcore_attach_video_buffer(const char* shmName);
INFCORE_API void infcore_detach_video_buffer();
INFCORE_API int  infcore_attach_result_buffer(const char* shmName, size_t bufferSize);
INFCORE_API void infcore_detach_result_buffer();

// ── Logging ───────────────────────────────────────────────────────────────────
typedef void (*InfCoreLogCallback)(int level, const char* msg, void* user);
INFCORE_API void infcore_set_log_callback(InfCoreLogCallback cb, void* user);
INFCORE_API void infcore_set_log_level(int level); // 0=debug 1=info 2=warn 3=error
