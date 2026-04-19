/*
 * InferenceCore.dll — Reconstructed C API Header
 * Recovered from: PE export table + string analysis
 * Original: MSVC x64, BCrypt/CUDA/TensorRT/WinInet
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

/** Initialize the inference engine subsystem.
 *  Loads CUDA runtime (cudart64_12.dll) from Python env.
 *  Checks TensorRT availability.
 *  Sets up shared memory: HELIOS_VIDEO_BUFFER, HELIOS_PROCESS_ID
 *  Returns: 0 on success */
int infcore_init(void);

/** Full shutdown. Frees CUDA/TRT resources. */
void infcore_shutdown(void);

/** Free engine build artifacts without shutting down. */
void infcore_cleanup_build(void);

/* ── Session / Auth ─────────────────────────────────────────────────────────── */

/** Set the Bearer session token obtained from session.dat (AES-GCM decrypted).
 *  Used in all subsequent API calls:
 *    Authorization: Bearer <token>
 *    X-Helios-API-Version: 4
 *    X-Discord-Username / X-Discord-Avatar headers
 *  @param session_token  null-terminated UTF-8 string */
void infcore_set_session(const char* session_token);

/** Returns last error string. NULL if no error. */
const char* infcore_get_last_error(void);

/** Returns accumulated debug log. */
const char* infcore_get_debug_log(void);

/* ── Model Operations ───────────────────────────────────────────────────────── */

/** List available models from /api/scripts/onnx/get-key-v2
 *  Response JSON: {"models":[{"uuid":"...","author":"...","encrypted":true,"requires_auth":true},...]}
 *  Returns: JSON string (caller must not free) */
const char* infcore_list_models(void);

/** Load an ONNX model by UUID or file path.
 *  Downloads encrypted model key from API.
 *  Decrypts weights with BCrypt AES.
 *  @param model_uuid_or_path  model UUID string or local .onnx path */
int infcore_load_model(const char* model_uuid_or_path);

/** Build TensorRT engine from loaded ONNX model.
 *  Long operation — caches result as encrypted .engine file.
 *  @param gpu_index  GPU to build on (see infcore_set_build_gpu) */
int infcore_build_engine(int gpu_index);

/** Probe ONNX file for input/output dimensions without loading.
 *  @param path  path to .onnx file
 *  Returns: JSON string with dimensions */
const char* infcore_probe_onnx_dimensions(const char* path);

/* ── Inference Control ──────────────────────────────────────────────────────── */

/** Start continuous inference on shared memory video ring buffer.
 *  Reads from: HELIOS_VIDEO_BUFFER shared memory
 *  Signals:    Global\Helios_<pid>_InferenceReady event when results ready */
int infcore_start_inference(void);

/** Stop inference loop. Does NOT free CUDA resources (see infcore_shutdown). */
void infcore_stop_inference(void);

/** Force process of next available frame. Used in single-shot mode. */
void infcore_trigger_frame(void);

/** Pause inference without stopping (keeps CUDA warm). */
void infcore_pause_engine(void);

/** Resume from paused state. */
void infcore_resume_engine(void);

/** Returns 1 if engine is paused, 0 otherwise. */
int infcore_is_paused(void);

/* ── GPU / Hardware ─────────────────────────────────────────────────────────── */

/** Returns number of CUDA-capable GPUs. */
int infcore_get_gpu_count(void);

/** Returns GPU name string for given index. */
const char* infcore_get_gpu_name(int gpu_index);

/** Returns 1 if TensorRT is available in current Python env. */
int infcore_has_tensorrt(void);

/** Set GPU index for engine build. */
void infcore_set_build_gpu(int gpu_index);

/* ── Shared Memory / IPC ────────────────────────────────────────────────────── */

/** Get the full name of the inference-complete event.
 *  Format: Global\Helios_<pid>_InferenceReady */
const char* infcore_get_complete_event_name(void);

/** Get the named shared memory segment for results.
 *  Format: Helios_<pid>_Results (inferred) */
const char* infcore_get_results_name(void);

/** Register an additional DLL path for model plugin loading. */
void infcore_add_dll_path(const char* path);

/* ── Detection Configuration ────────────────────────────────────────────────── */

/* Region of Interest */
void infcore_set_roi(float x, float y, float w, float h);
void infcore_set_ignore_region(float x, float y, float w, float h);
void infcore_set_anchor_point(float x, float y);
void infcore_set_anchor_point_for_classes(const char* class_ids_json, float x, float y);
void infcore_set_polar_origin(float x, float y);
void infcore_set_polar_origin_ring(float radius);

/* Confidence thresholds */
void infcore_set_confidence(float threshold);
void infcore_set_confidence_for_classes(const char* class_ids_json, float threshold);
void infcore_set_nms_threshold(float threshold);

/* Keypoints (pose/skeleton) */
void infcore_set_keypoint_conf_threshold(float threshold);
void infcore_set_keypoint_mask(const char* keypoint_ids_json);
void infcore_set_keypoint_radius(int radius);

/* Class priority / override */
void infcore_set_class_priority(const char* class_id, int priority);
void infcore_set_class_overrides(const char* overrides_json);  /* inferred */
void infcore_clear_class_overrides(void);

/* Tracking */
void infcore_set_tracking(int enabled);

/* Sort method */
void infcore_set_sort_method(int method);  /* enum: distance, confidence, size, etc. */

/* ── Search / Button Detection ──────────────────────────────────────────────── */
/* Used for HUD element detection (shot meter, icons, OOP indicators) */
void infcore_set_search_mode(int mode);
void infcore_set_search_buttons(const char* buttons_json);
void infcore_set_search_button_conditions(const char* conditions_json);
void infcore_set_search_clauses(const char* clauses_json);

/* ── Zoom ────────────────────────────────────────────────────────────────────── */
void infcore_set_zoom_enabled(int enabled);
void infcore_set_zoom_buttons(const char* buttons_json);
void infcore_set_zoom_button_enabled(int enabled);
void infcore_set_zoom_max(float max_scale);
void infcore_set_zoom_step(float step);
void infcore_set_zoom_hold_ms(int ms);

/* ── Draw / Overlay ─────────────────────────────────────────────────────────── */
void infcore_set_draw_detections(int enabled);
void infcore_set_draw_roi(int enabled);
void infcore_set_draw_skeleton(int enabled);
void infcore_set_draw_keypoints(int enabled);
void infcore_set_draw_anchor_point(int enabled);
void infcore_set_draw_origin_cross(int enabled);
void infcore_set_draw_origin_line(int enabled);
void infcore_set_draw_ignore_region(int enabled);
void infcore_set_draw_zoomed_roi(int enabled);
void infcore_set_draw_benchmarks(int enabled);

/* Colors (ARGB hex or "#RRGGBBAA") */
void infcore_set_color_bbox(const char* color);
void infcore_set_color_bbox_for_classes(const char* class_ids_json, const char* color);
void infcore_set_color_skeleton(const char* color);
void infcore_set_color_keypoints(const char* color);
void infcore_set_color_anchor(const char* color);
void infcore_set_color_anchor_for_classes(const char* class_ids_json, const char* color);
void infcore_set_color_origin(const char* color);
void infcore_set_color_origin_line(const char* color);
void infcore_set_color_roi(const char* color);
void infcore_set_color_ignore_region(const char* color);
void infcore_set_color_zoomed_roi(const char* color);

#ifdef __cplusplus
}
#endif
