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

/* AppLayerTxData definition */
typedef struct AppLayerTxData {
    /** Flags shared with app-layer parser */
    uint32_t flags;
    /** Used by tx logging. */
    DetectEngineState *de_state;
    /** Detection engine flags */
    uint64_t detect_flags;
    /** Offset into the flow's payload_inspection_progress. Only set in case of
     *  midstream pickups, where we may have a non-zero starting offset. */
    uint64_t progress_first_offset;
    /** Inspection recursion level */
    uint32_t inspect_recursion_level;
} AppLayerTxData;

/* AppLayerTxConfig definition */
typedef uint64_t AppLayerTxConfig;

#endif /* __APP_LAYER_TYPES_H__ */ 