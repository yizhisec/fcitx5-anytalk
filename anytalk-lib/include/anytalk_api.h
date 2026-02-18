#ifndef ANYTALK_API_H
#define ANYTALK_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnytalkContext AnytalkContext;

typedef enum {
    ANYTALK_EVENT_PARTIAL = 0,
    ANYTALK_EVENT_FINAL   = 1,
    ANYTALK_EVENT_STATUS  = 2,
    ANYTALK_EVENT_ERROR   = 3,
} AnytalkEventType;

typedef void (*AnytalkEventCallback)(void *user_data, AnytalkEventType type, const char *text);

typedef struct {
    const char *app_id;
    const char *access_token;
    const char *resource_id;  /* NULL = default "volc.seedasr.sauc.duration" */
    const char *mode;         /* NULL = default "bidi_async" */
} AnytalkConfig;

AnytalkContext *anytalk_init(const AnytalkConfig *config, AnytalkEventCallback cb, void *user_data);
void anytalk_destroy(AnytalkContext *ctx);
int anytalk_start(AnytalkContext *ctx);
int anytalk_stop(AnytalkContext *ctx);
int anytalk_cancel(AnytalkContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ANYTALK_API_H */
