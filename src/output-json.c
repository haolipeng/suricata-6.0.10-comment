/* Copyright (C) 2007-2020 Open Information Security Foundation
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
 * \author Tom DeCanio <td@npulsetech.com>
 *
 * Logs detection and monitoring events in JSON format.
 *
 */

#include "suricata-common.h"
#include "debug.h"
#include "detect.h"
#include "flow.h"
#include "conf.h"

#include "threads.h"
#include "tm-threads.h"
#include "threadvars.h"
#include "util-debug.h"

#include "util-unittest.h"
#include "util-unittest-helper.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-reference.h"
#include "app-layer-parser.h"
#include "util-classification-config.h"
#include "util-syslog.h"

#include "output.h"
#include "output-json.h"

#include "util-byte.h"
#include "util-privs.h"
#include "util-print.h"
#include "util-proto-name.h"
#include "util-optimize.h"
#include "util-buffer.h"
#include "util-logopenfile.h"
#include "util-log-redis.h"
#include "util-device.h"
#include "util-validate.h"
#include "util-crypt.h"
#include "util-plugin.h"

#include "flow-var.h"
#include "flow-bit.h"
#include "flow-storage.h"

#include "source-pcap-file.h"

#include "suricata-plugin.h"

#define DEFAULT_LOG_FILENAME "eve.json"
#define DEFAULT_ALERT_SYSLOG_FACILITY_STR       "local0"
#define DEFAULT_ALERT_SYSLOG_FACILITY           LOG_LOCAL0
#define DEFAULT_ALERT_SYSLOG_LEVEL              LOG_INFO
#define MODULE_NAME "OutputJSON"

#define MAX_JSON_SIZE 2048

static void OutputJsonDeInitCtx(OutputCtx *);

static const char *TRAFFIC_ID_PREFIX = "traffic/id/";
static const char *TRAFFIC_LABEL_PREFIX = "traffic/label/";
static size_t traffic_id_prefix_len = 0;
static size_t traffic_label_prefix_len = 0;

const JsonAddrInfo json_addr_info_zero;

void OutputJsonRegister (void)
{
    OutputRegisterModule(MODULE_NAME, "eve-log", OutputJsonInitCtx);

    traffic_id_prefix_len = strlen(TRAFFIC_ID_PREFIX);
    traffic_label_prefix_len = strlen(TRAFFIC_LABEL_PREFIX);
}

json_t *SCJsonBool(int val)
{
    return (val ? json_true() : json_false());
}

/**
 * Wrap json_decref. This is mainly to expose this function to Rust as its
 * defined in the Jansson header file as an inline function.
 */
void SCJsonDecref(json_t *json)
{
    json_decref(json);
}

json_t *SCJsonString(const char *val)
{
    if (val == NULL){
        return NULL;
    }
    json_t * retval = json_string(val);
    char retbuf[MAX_JSON_SIZE] = {0};
    if (retval == NULL) {
        uint32_t u = 0;
        uint32_t offset = 0;
        for (u = 0; u < strlen(val); u++) {
            if (isprint(val[u])) {
                PrintBufferData(retbuf, &offset, MAX_JSON_SIZE-1, "%c",
                        val[u]);
            } else {
                PrintBufferData(retbuf, &offset, MAX_JSON_SIZE-1,
                        "\\x%02X", val[u]);
            }
        }
        retbuf[offset] = '\0';
        retval = json_string(retbuf);
    }
    return retval;
}

/* Default Sensor ID value */
static int64_t sensor_id = -1; /* -1 = not defined */

void JsonAddrInfoInit(const Packet *p, enum OutputJsonLogDirection dir, JsonAddrInfo *addr)
{
    char srcip[46] = {0}, dstip[46] = {0};
    Port sp, dp;

    switch (dir) {
        case LOG_DIR_PACKET:
            if (PKT_IS_IPV4(p)) {
                PrintInet(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p),
                        srcip, sizeof(srcip));
                PrintInet(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p),
                        dstip, sizeof(dstip));
            } else if (PKT_IS_IPV6(p)) {
                PrintInet(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p),
                        srcip, sizeof(srcip));
                PrintInet(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p),
                        dstip, sizeof(dstip));
            } else {
                /* Not an IP packet so don't do anything */
                return;
            }
            sp = p->sp;
            dp = p->dp;
            break;
        case LOG_DIR_FLOW:
        case LOG_DIR_FLOW_TOSERVER:
            if ((PKT_IS_TOSERVER(p))) {
                if (PKT_IS_IPV4(p)) {
                    PrintInet(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p),
                            dstip, sizeof(dstip));
                } else if (PKT_IS_IPV6(p)) {
                    PrintInet(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p),
                            dstip, sizeof(dstip));
                }
                sp = p->sp;
                dp = p->dp;
            } else {
                if (PKT_IS_IPV4(p)) {
                    PrintInet(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p),
                            dstip, sizeof(dstip));
                } else if (PKT_IS_IPV6(p)) {
                    PrintInet(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p),
                            dstip, sizeof(dstip));
                }
                sp = p->dp;
                dp = p->sp;
            }
            break;
        case LOG_DIR_FLOW_TOCLIENT:
            if ((PKT_IS_TOCLIENT(p))) {
                if (PKT_IS_IPV4(p)) {
                    PrintInet(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p),
                            dstip, sizeof(dstip));
                } else if (PKT_IS_IPV6(p)) {
                    PrintInet(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p),
                            dstip, sizeof(dstip));
                }
                sp = p->sp;
                dp = p->dp;
            } else {
                if (PKT_IS_IPV4(p)) {
                    PrintInet(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p),
                            dstip, sizeof(dstip));
                } else if (PKT_IS_IPV6(p)) {
                    PrintInet(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p),
                            srcip, sizeof(srcip));
                    PrintInet(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p),
                            dstip, sizeof(dstip));
                }
                sp = p->dp;
                dp = p->sp;
            }
            break;
        default:
            DEBUG_VALIDATE_BUG_ON(1);
            return;
    }


    strlcpy(addr->src_ip, srcip, JSON_ADDR_LEN);

    switch(p->proto) {
        case IPPROTO_ICMP:
            break;
        case IPPROTO_UDP:
        case IPPROTO_TCP:
        case IPPROTO_SCTP:
            addr->sp = sp;
            break;
    }

    strlcpy(addr->dst_ip, dstip, JSON_ADDR_LEN);

    switch(p->proto) {
        case IPPROTO_ICMP:
            break;
        case IPPROTO_UDP:
        case IPPROTO_TCP:
        case IPPROTO_SCTP:
            addr->dp = dp;
            break;
    }

    if (SCProtoNameValid(IP_GET_IPPROTO(p))) {
        strlcpy(addr->proto, known_proto[IP_GET_IPPROTO(p)], sizeof(addr->proto));
    } else {
        snprintf(addr->proto, sizeof(addr->proto), "%" PRIu32, IP_GET_IPPROTO(p));
    }
}

#define COMMUNITY_ID_BUF_SIZE 64

static inline bool FlowHashRawAddressIPv6LtU32(const uint32_t *a, const uint32_t *b)
{
    for (int i = 0; i < 4; i++) {
        if (a[i] < b[i])
            return true;
        if (a[i] > b[i])
            break;
    }

    return false;
}

int OutputJSONMemBufferCallback(const char *str, size_t size, void *data)
{
    OutputJSONMemBufferWrapper *wrapper = data;
    MemBuffer **memb = wrapper->buffer;

    if (MEMBUFFER_OFFSET(*memb) + size >= MEMBUFFER_SIZE(*memb)) {
        MemBufferExpand(memb, wrapper->expand_by);
    }

    MemBufferWriteRaw((*memb), str, size);
    return 0;
}

int OutputJSONBuffer(json_t *js, LogFileCtx *file_ctx, MemBuffer **buffer)
{
    if (file_ctx->sensor_name) {
        json_object_set_new(js, "host",
                            json_string(file_ctx->sensor_name));
    }

    if (file_ctx->is_pcap_offline) {
        json_object_set_new(js, "pcap_filename", json_string(PcapFileGetFilename()));
    }

    if (file_ctx->prefix) {
        MemBufferWriteRaw((*buffer), file_ctx->prefix, file_ctx->prefix_len);
    }

    OutputJSONMemBufferWrapper wrapper = {
        .buffer = buffer,
        .expand_by = JSON_OUTPUT_BUFFER_SIZE
    };

    int r = json_dump_callback(js, OutputJSONMemBufferCallback, &wrapper,
            file_ctx->json_flags);
    if (r != 0)
        return TM_ECODE_OK;

    LogFileWrite(file_ctx, *buffer);
    return 0;
}

/**
 * \brief Create a new LogFileCtx for "fast" output style.
 * \param conf The configuration node for this output.
 * \return A LogFileCtx pointer on success, NULL on failure.
 */
OutputInitResult OutputJsonInitCtx(ConfNode *conf)
{
    OutputInitResult result = { NULL, false };

    OutputJsonCtx *json_ctx = SCCalloc(1, sizeof(OutputJsonCtx));
    if (unlikely(json_ctx == NULL)) {
        SCLogDebug("could not create new OutputJsonCtx");
        return result;
    }

    /* First lookup a sensor-name value in this outputs configuration
     * node (deprecated). If that fails, lookup the global one. */
    const char *sensor_name = ConfNodeLookupChildValue(conf, "sensor-name");
    if (sensor_name != NULL) {
        SCLogWarning(SC_ERR_DEPRECATED_CONF,
            "Found deprecated eve-log setting \"sensor-name\". "
            "Please set sensor-name globally.");
    }
    else {
        (void)ConfGet("sensor-name", &sensor_name);
    }

    json_ctx->file_ctx = LogFileNewCtx();
    if (unlikely(json_ctx->file_ctx == NULL)) {
        SCLogDebug("AlertJsonInitCtx: Could not create new LogFileCtx");
        SCFree(json_ctx);
        return result;
    }

    if (sensor_name) {
        json_ctx->file_ctx->sensor_name = SCStrdup(sensor_name);
        if (json_ctx->file_ctx->sensor_name  == NULL) {
            LogFileFreeCtx(json_ctx->file_ctx);
            SCFree(json_ctx);
            return result;
        }
    } else {
        json_ctx->file_ctx->sensor_name = NULL;
    }

    OutputCtx *output_ctx = SCCalloc(1, sizeof(OutputCtx));
    if (unlikely(output_ctx == NULL)) {
        LogFileFreeCtx(json_ctx->file_ctx);
        SCFree(json_ctx);
        return result;
    }

    output_ctx->data = json_ctx;
    output_ctx->DeInit = OutputJsonDeInitCtx;

    if (conf) {
        const char *output_s = ConfNodeLookupChildValue(conf, "filetype");

        // Backwards compatibility
        if (output_s == NULL) {
            output_s = ConfNodeLookupChildValue(conf, "type");
        }

        if (output_s != NULL) {
            if (strcmp(output_s, "file") == 0 ||
                strcmp(output_s, "regular") == 0) {
                json_ctx->json_out = LOGFILE_TYPE_FILE;
            } else if (strcmp(output_s, "syslog") == 0) {
                json_ctx->json_out = LOGFILE_TYPE_SYSLOG;
            } else if (strcmp(output_s, "unix_dgram") == 0) {
                json_ctx->json_out = LOGFILE_TYPE_UNIX_DGRAM;
            } else if (strcmp(output_s, "unix_stream") == 0) {
                json_ctx->json_out = LOGFILE_TYPE_UNIX_STREAM;
            } else if (strcmp(output_s, "redis") == 0) {
#ifdef HAVE_LIBHIREDIS
                SCLogRedisInit();
                json_ctx->json_out = LOGFILE_TYPE_REDIS;
#else
                           FatalError(SC_ERR_FATAL,
                                      "redis JSON output option is not compiled");
#endif
            } else {
#ifdef HAVE_PLUGINS
                SCPluginFileType *plugin = SCPluginFindFileType(output_s);
                if (plugin == NULL) {
                    FatalError(SC_ERR_INVALID_ARGUMENT,
                            "Invalid JSON output option: %s", output_s);
                } else {
                    json_ctx->json_out = LOGFILE_TYPE_PLUGIN;
                    json_ctx->plugin = plugin;
                }
#else
                FatalError(SC_ERR_INVALID_ARGUMENT,
                        "Invalid JSON output option: %s", output_s);
#endif
            }
        }

        const char *prefix = ConfNodeLookupChildValue(conf, "prefix");
        if (prefix != NULL)
        {
            SCLogInfo("Using prefix '%s' for JSON messages", prefix);
            json_ctx->file_ctx->prefix = SCStrdup(prefix);
            if (json_ctx->file_ctx->prefix == NULL)
            {
                    FatalError(SC_ERR_FATAL,
                               "Failed to allocate memory for eve-log.prefix setting.");
            }
            json_ctx->file_ctx->prefix_len = strlen(prefix);
        }

        if (json_ctx->json_out == LOGFILE_TYPE_FILE ||
            json_ctx->json_out == LOGFILE_TYPE_UNIX_DGRAM ||
            json_ctx->json_out == LOGFILE_TYPE_UNIX_STREAM)
        {
            if (json_ctx->json_out == LOGFILE_TYPE_FILE) {
                /* Threaded file output */
                const ConfNode *threaded = ConfNodeLookupChild(conf, "threaded");
                if (threaded && threaded->val && ConfValIsTrue(threaded->val)) {
                    SCLogConfig("Enabling threaded eve logging.");
                    json_ctx->file_ctx->threaded = true;
                } else {
                    json_ctx->file_ctx->threaded = false;
                }
            }

            if (SCConfLogOpenGeneric(conf, json_ctx->file_ctx, DEFAULT_LOG_FILENAME, 1) < 0) {
                LogFileFreeCtx(json_ctx->file_ctx);
                SCFree(json_ctx);
                SCFree(output_ctx);
                return result;
            }
            OutputRegisterFileRotationFlag(&json_ctx->file_ctx->rotation_flag);

        }
#ifndef OS_WIN32
	else if (json_ctx->json_out == LOGFILE_TYPE_SYSLOG) {
            const char *facility_s = ConfNodeLookupChildValue(conf, "facility");
            if (facility_s == NULL) {
                facility_s = DEFAULT_ALERT_SYSLOG_FACILITY_STR;
            }

            int facility = SCMapEnumNameToValue(facility_s, SCSyslogGetFacilityMap());
            if (facility == -1) {
                SCLogWarning(SC_ERR_INVALID_ARGUMENT, "Invalid syslog facility: \"%s\","
                        " now using \"%s\" as syslog facility", facility_s,
                        DEFAULT_ALERT_SYSLOG_FACILITY_STR);
                facility = DEFAULT_ALERT_SYSLOG_FACILITY;
            }

            const char *level_s = ConfNodeLookupChildValue(conf, "level");
            if (level_s != NULL) {
                int level = SCMapEnumNameToValue(level_s, SCSyslogGetLogLevelMap());
                if (level != -1) {
                    json_ctx->file_ctx->syslog_setup.alert_syslog_level = level;
                }
            }

            const char *ident = ConfNodeLookupChildValue(conf, "identity");
            /* if null we just pass that to openlog, which will then
             * figure it out by itself. */

            openlog(ident, LOG_PID|LOG_NDELAY, facility);
        }
#endif
#ifdef HAVE_LIBHIREDIS
        else if (json_ctx->json_out == LOGFILE_TYPE_REDIS) {
            ConfNode *redis_node = ConfNodeLookupChild(conf, "redis");
            if (!json_ctx->file_ctx->sensor_name) {
                char hostname[1024];
                gethostname(hostname, 1023);
                json_ctx->file_ctx->sensor_name = SCStrdup(hostname);
            }
            if (json_ctx->file_ctx->sensor_name  == NULL) {
                LogFileFreeCtx(json_ctx->file_ctx);
                SCFree(json_ctx);
                SCFree(output_ctx);
                return result;
            }

            if (SCConfLogOpenRedis(redis_node, json_ctx->file_ctx) < 0) {
                LogFileFreeCtx(json_ctx->file_ctx);
                SCFree(json_ctx);
                SCFree(output_ctx);
                return result;
            }
        }
#endif
        else if (json_ctx->json_out == LOGFILE_TYPE_PLUGIN) {
            ConfNode *plugin_conf = ConfNodeLookupChild(conf,
                json_ctx->plugin->name);
            void *plugin_data = NULL;
            if (json_ctx->plugin->Open(plugin_conf, &plugin_data) < 0) {
                LogFileFreeCtx(json_ctx->file_ctx);
                SCFree(json_ctx);
                SCFree(output_ctx);
                return result;
            } else {
                json_ctx->file_ctx->plugin = json_ctx->plugin;
                json_ctx->file_ctx->plugin_data = plugin_data;
            }
        }

        const char *sensor_id_s = ConfNodeLookupChildValue(conf, "sensor-id");
        if (sensor_id_s != NULL) {
            if (StringParseUint64((uint64_t *)&sensor_id, 10, 0, sensor_id_s) < 0) {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                           "Failed to initialize JSON output, "
                           "invalid sensor-id: %s", sensor_id_s);
                exit(EXIT_FAILURE);
            }
        }

        /* Check if top-level metadata should be logged. */
        const ConfNode *metadata = ConfNodeLookupChild(conf, "metadata");
        if (metadata && metadata->val && ConfValIsFalse(metadata->val)) {
            SCLogConfig("Disabling eve metadata logging.");
            json_ctx->cfg.include_metadata = false;
        } else {
            json_ctx->cfg.include_metadata = true;
        }

        /* Check if ethernet information should be logged. */
        const ConfNode *ethernet = ConfNodeLookupChild(conf, "ethernet");
        if (ethernet && ethernet->val && ConfValIsTrue(ethernet->val)) {
            SCLogConfig("Enabling Ethernet MAC address logging.");
            json_ctx->cfg.include_ethernet = true;
        } else {
            json_ctx->cfg.include_ethernet = false;
        }

        /* See if we want to enable the community id */
        const ConfNode *community_id = ConfNodeLookupChild(conf, "community-id");
        if (community_id && community_id->val && ConfValIsTrue(community_id->val)) {
            SCLogConfig("Enabling eve community_id logging.");
            json_ctx->cfg.include_community_id = true;
        } else {
            json_ctx->cfg.include_community_id = false;
        }
        const char *cid_seed = ConfNodeLookupChildValue(conf, "community-id-seed");
        if (cid_seed != NULL) {
            if (StringParseUint16(&json_ctx->cfg.community_id_seed,
                        10, 0, cid_seed) < 0)
            {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                           "Failed to initialize JSON output, "
                           "invalid community-id-seed: %s", cid_seed);
                exit(EXIT_FAILURE);
            }
        }

        /* Do we have a global eve xff configuration? */
        const ConfNode *xff = ConfNodeLookupChild(conf, "xff");
        if (xff != NULL) {
            json_ctx->xff_cfg = SCCalloc(1, sizeof(HttpXFFCfg));
            if (likely(json_ctx->xff_cfg != NULL)) {
                HttpXFFGetCfg(conf, json_ctx->xff_cfg);
            }
        }

        const char *pcapfile_s = ConfNodeLookupChildValue(conf, "pcap-file");
        if (pcapfile_s != NULL && ConfValIsTrue(pcapfile_s)) {
            json_ctx->file_ctx->is_pcap_offline =
                (RunmodeGetCurrent() == RUNMODE_PCAP_FILE ||
                 RunmodeGetCurrent() == RUNMODE_UNIX_SOCKET);
        }

        json_ctx->file_ctx->type = json_ctx->json_out;
    }

    SCLogDebug("returning output_ctx %p", output_ctx);

    result.ctx = output_ctx;
    result.ok = true;
    return result;
}

static void OutputJsonDeInitCtx(OutputCtx *output_ctx)
{
    OutputJsonCtx *json_ctx = (OutputJsonCtx *)output_ctx->data;
    LogFileCtx *logfile_ctx = json_ctx->file_ctx;
    if (logfile_ctx->dropped) {
        SCLogWarning(SC_WARN_EVENT_DROPPED,
                "%"PRIu64" events were dropped due to slow or "
                "disconnected socket", logfile_ctx->dropped);
    }
    if (json_ctx->xff_cfg != NULL) {
        SCFree(json_ctx->xff_cfg);
    }
    LogFileFreeCtx(logfile_ctx);
    SCFree(json_ctx);
    SCFree(output_ctx);
}
