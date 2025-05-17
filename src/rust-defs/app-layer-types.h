/* App-layer types previously defined in rust-generated headers */
#ifndef __APP_LAYER_TYPES_H__
#define __APP_LAYER_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
/* Include detect-engine-state.h for DetectEngineState */
#include "../detect-engine-state.h"

/* AppLayerResult definition */
typedef struct AppLayerResult {
    int status;
    uint16_t consumed;
    uint16_t needed;
} AppLayerResult;

/* AppLayerGetTxIterTuple definition */
typedef struct AppLayerGetTxIterTuple {
    void *tx_ptr;
    uint64_t tx_id;
    bool has_next;
} AppLayerGetTxIterTuple;

typedef struct AppLayerTxConfig {
    /**
     * config: log flags
     */
    uint8_t log_flags;
} AppLayerTxConfig;

/**
 * LoggerFlags tracks which loggers have already been executed.
 */
typedef struct LoggerFlags {
    uint32_t flags;
} LoggerFlags;

typedef struct AppLayerTxData {
    /**
     * config: log flags
     */
    struct AppLayerTxConfig config;
    /**
     * logger flags for tx logging api
     */
    struct LoggerFlags logged;
    /**
     * track file open/logs so we can know how long to keep the tx
     */
    uint32_t files_opened;
    uint32_t files_logged;
    uint32_t files_stored;
    /**
     * detection engine flags for use by detection engine
     */
    uint64_t detect_flags_ts;
    uint64_t detect_flags_tc;
} AppLayerTxData;

#endif /* __APP_LAYER_TYPES_H__ */ 