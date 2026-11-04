/* PLAYTEST.FUN SDK - C API (stable ABI for Unity, Godot, FFI). */
#ifndef PLAYTESTFUN_H
#define PLAYTESTFUN_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(PTF_SHARED_BUILD)
#    define PTF_API __declspec(dllexport)
#  elif defined(PTF_SHARED)
#    define PTF_API __declspec(dllimport)
#  else
#    define PTF_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define PTF_API __attribute__((visibility("default")))
#else
#  define PTF_API
#endif

#define PTF_VERSION "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ptf_log_level {
    PTF_LOG_DEBUG = 0,
    PTF_LOG_INFO = 1,
    PTF_LOG_WARN = 2,
    PTF_LOG_ERROR = 3
} ptf_log_level;

/* Called from the SDK worker thread. */
typedef void (*ptf_log_fn)(int level, const char* message, void* user);

/* Optional custom HTTP (consoles, Android, engine HTTP). Blocking, runs on the worker thread.
   headers: "Name: value\r\n" lines. Return HTTP status, 0 on network failure. */
typedef int (*ptf_http_fn)(void* user, const char* method, const char* url, const char* headers,
                           const char* body, char* out_body, size_t out_capacity, int timeout_ms);

typedef struct ptf_config {
    uint32_t struct_size;          /* sizeof(ptf_config), set by ptf_config_init */
    const char* game_key;          /* phub_gk_... (required) */
    const char* playtest_id;       /* optional */
    const char* base_url;          /* NULL = https://api.playtest.fun/v1 */
    const char* storage_dir;       /* UTF-8, NULL = no offline persistence */
    const char* exit_url;          /* NULL = https://playtest.fun */
    int32_t open_url_on_shutdown;  /* 1 = open exit_url in ptf_shutdown */
    uint32_t heartbeat_seconds;    /* 0 = 60 */
    uint32_t shutdown_timeout_ms;  /* 0 = 2000 */
    ptf_log_fn log;
    void* log_user;
    ptf_http_fn http;              /* NULL = platform default */
    void* http_user;
    const char* build_version;     /* optional, e.g. "0.8.4a": session id prefix + crash buildVersion */
} ptf_config;

typedef struct ptf_stats {
    double local_session_seconds;   /* measured on this device */
    double server_session_seconds;  /* sessionPlaytimeSeconds from last reply */
    double server_total_seconds;    /* totalPlaytimeSeconds in this playtest */
    int32_t last_http_status;       /* 0 = none / network error */
    int32_t player_linked;          /* -1 unknown, 0 no, 1 yes */
    int32_t player_accepted;        /* -1 unknown, 0 no, 1 yes */
    char last_error[64];            /* API error.code, empty if none */
    char session_id[80];
    int32_t pending_events;         /* events not delivered yet */
} ptf_stats;

PTF_API void ptf_config_init(ptf_config* config);

/* Returns 1 on success. Calling again replaces the running session. */
PTF_API int ptf_init(const ptf_config* config);

/* Nothing is sent until the SteamID64 is known. Returns 1 if valid. */
PTF_API int ptf_set_steam_id(uint64_t steam_id64);
PTF_API int ptf_set_steam_id_str(const char* steam_id64);

/* Stop counting play time while the app is in background. */
PTF_API void ptf_set_paused(int paused);

/* Free-form JSON object sent as session "data" (max 16 KB). */
PTF_API int ptf_set_session_data(const char* json_object);

/* Events: queued (on disk with storage_dir), retried until delivered. Return 1 if queued. */
PTF_API int ptf_goal(const char* key);                    /* goal without a target: done */
PTF_API int ptf_goal_value(const char* key, double value); /* goal with a target */
PTF_API int ptf_test_result(int test_id, int passed, const char* message /* nullable */);
PTF_API int ptf_crash(const char* fingerprint, const char* message, const char* stack); /* nullable */
/* Any event; data_json is a JSON object (max 32 KB), trusted as is. */
PTF_API int ptf_event(const char* event_type, const char* status, const char* data_json);

/* Send a heartbeat and queued events now (async). */
PTF_API void ptf_flush(void);

PTF_API int ptf_get_stats(ptf_stats* out);

/* Final send with ended_at (bounded by shutdown_timeout_ms), then opens exit_url if enabled. */
PTF_API void ptf_shutdown(void);

/* Opens an http(s) URL in the default browser (desktop only). Returns 1 on success. */
PTF_API int ptf_open_url(const char* url);

PTF_API const char* ptf_version(void);

#ifdef __cplusplus
}
#endif

#endif /* PLAYTESTFUN_H */
