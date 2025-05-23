/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Endace Technology Limited, Jason Ish <jason.ish@endace.com>
 */

#ifndef __OUTPUT_H__
#define __OUTPUT_H__

#include "suricata.h"
#include "tm-threads.h"

#define DEFAULT_LOG_MODE_APPEND     "yes"
#define DEFAULT_LOG_FILETYPE        "regular"

#include "output-packet.h"
#include "output-tx.h"
#include "output-stats.h"

#include "util-config.h"

typedef struct OutputInitResult_ {
    OutputCtx *ctx;
    bool ok;
} OutputInitResult;

typedef OutputInitResult (*OutputInitFunc)(ConfNode *);
typedef OutputInitResult (*OutputInitSubFunc)(ConfNode *, OutputCtx *);
typedef TmEcode (*OutputLogFunc)(ThreadVars *, Packet *, void *);
typedef uint32_t (*OutputGetActiveCountFunc)(void);

typedef struct OutputModule_ {
    LoggerId logger_id;
    const char *name;
    const char *conf_name;
    const char *parent_name;
    OutputInitFunc InitFunc;
    OutputInitSubFunc InitSubFunc;

    ThreadInitFunc ThreadInit;
    ThreadDeinitFunc ThreadDeinit;
    ThreadExitPrintStatsFunc ThreadExitPrintStats;

    PacketLogger PacketLogFunc;
    PacketLogCondition PacketConditionFunc;
    TxLogger TxLogFunc;
    TxLoggerCondition TxLogCondition;
    StatsLogger StatsLogFunc;
    AppProto alproto;
    int tc_log_progress;
    int ts_log_progress;

    TAILQ_ENTRY(OutputModule_) entries;
} OutputModule;

typedef TAILQ_HEAD(OutputModuleList_, OutputModule_) OutputModuleList;
extern OutputModuleList output_modules;

void OutputRegisterModule(const char *, const char *, OutputInitFunc);

void OutputRegisterPacketModule(LoggerId id, const char *name,
    const char *conf_name, OutputInitFunc InitFunc, PacketLogger LogFunc,
    PacketLogCondition ConditionFunc, ThreadInitFunc, ThreadDeinitFunc,
    ThreadExitPrintStatsFunc);
void OutputRegisterPacketSubModule(LoggerId id, const char *parent_name,
    const char *name, const char *conf_name, OutputInitSubFunc InitFunc,
    PacketLogger LogFunc, PacketLogCondition ConditionFunc,
    ThreadInitFunc ThreadInit, ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);

void OutputRegisterTxModule(LoggerId id, const char *name,
    const char *conf_name, OutputInitFunc InitFunc, AppProto alproto,
    TxLogger TxLogFunc, ThreadInitFunc ThreadInit,
    ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);
void OutputRegisterTxSubModule(LoggerId id, const char *parent_name,
    const char *name, const char *conf_name,
    OutputInitSubFunc InitFunc, AppProto alproto, TxLogger TxLogFunc,
    ThreadInitFunc ThreadInit, ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);

void OutputRegisterTxModuleWithCondition(LoggerId id, const char *name,
    const char *conf_name, OutputInitFunc InitFunc, AppProto alproto,
    TxLogger TxLogFunc, TxLoggerCondition TxLogCondition,
    ThreadInitFunc ThreadInit, ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);
void OutputRegisterTxSubModuleWithCondition(LoggerId id,
    const char *parent_name, const char *name, const char *conf_name,
    OutputInitSubFunc InitFunc, AppProto alproto,
    TxLogger TxLogFunc, TxLoggerCondition TxLogCondition,
    ThreadInitFunc ThreadInit, ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);

void OutputRegisterTxModuleWithProgress(LoggerId id, const char *name,
    const char *conf_name, OutputInitFunc InitFunc, AppProto alproto,
    TxLogger TxLogFunc, int tc_log_progress, int ts_log_progress,
    ThreadInitFunc ThreadInit, ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);
void OutputRegisterTxSubModuleWithProgress(LoggerId id, const char *parent_name,
    const char *name, const char *conf_name,
    OutputInitSubFunc InitFunc, AppProto alproto, TxLogger TxLogFunc,
    int tc_log_progress, int ts_log_progress, ThreadInitFunc ThreadInit,
    ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);

void OutputRegisterStatsModule(LoggerId id, const char *name,
    const char *conf_name, OutputInitFunc InitFunc,
    StatsLogger StatsLogFunc, ThreadInitFunc ThreadInit,
    ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);
void OutputRegisterStatsSubModule(LoggerId id, const char *parent_name,
    const char *name, const char *conf_name,
    OutputInitSubFunc InitFunc,
    StatsLogger StatsLogFunc, ThreadInitFunc ThreadInit,
    ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats);

OutputModule *OutputGetModuleByConfName(const char *name);
void OutputDeregisterAll(void);

int OutputDropLoggerEnable(void);
void OutputDropLoggerDisable(void);

void OutputRegisterFileRotationFlag(int *flag);
void OutputUnregisterFileRotationFlag(int *flag);
void OutputNotifyFileRotation(void);

void OutputRegisterRootLogger(ThreadInitFunc ThreadInit,
    ThreadDeinitFunc ThreadDeinit,
    ThreadExitPrintStatsFunc ThreadExitPrintStats,
    OutputLogFunc LogFunc, OutputGetActiveCountFunc ActiveCntFunc);
void TmModuleLoggerRegister(void);

TmEcode OutputLoggerLog(ThreadVars *, Packet *, void *);
TmEcode OutputLoggerThreadInit(ThreadVars *, const void *, void **);
TmEcode OutputLoggerThreadDeinit(ThreadVars *, void *);
void OutputLoggerExitPrintStats(ThreadVars *, void *);

void OutputSetupActiveLoggers(void);
void OutputClearActiveLoggers(void);

#endif /* ! __OUTPUT_H__ */
