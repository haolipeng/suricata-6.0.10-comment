/* Copyright (C) 2007-2013 Open Information Security Foundation
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
 * \author Victor Julien <victor@inliniac.net>
 *
 * Flow utility functions
 */

#include "suricata-common.h"
#include "threads.h"

#include "flow.h"
#include "flow-private.h"
#include "flow-util.h"
#include "flow-var.h"
#include "app-layer.h"

#include "util-var.h"
#include "util-debug.h"
#include "flow-storage.h"

#include "detect.h"
#include "detect-engine-state.h"

#include "util-validate.h"

/** \brief allocate a flow
 *
 *  We check against the memuse counter. If it passes that check we increment
 *  the counter first, then we try to alloc.
 *
 *  \retval f the flow or NULL on out of memory
 */
Flow *FlowAlloc(void)
{
    Flow *f;
    size_t size = sizeof(Flow) + FlowStorageSize();

	//检测是否超出内存最大值
    if (!(FLOW_CHECK_MEMCAP(size))) {
        return NULL;
    }

    (void) SC_ATOMIC_ADD(flow_memuse, size);

    f = SCMalloc(size);
    if (unlikely(f == NULL)) {
        (void)SC_ATOMIC_SUB(flow_memuse, size);
        return NULL;
    }
    memset(f, 0, size);

    /* coverity[missing_lock] */
    FLOW_INITIALIZE(f);
    return f;
}


/**
 *  \brief cleanup & free the memory of a flow
 *
 *  \param f flow to clear & destroy
 */
void FlowFree(Flow *f)
{
    FLOW_DESTROY(f);
    SCFree(f);

    size_t size = sizeof(Flow) + FlowStorageSize();
    (void) SC_ATOMIC_SUB(flow_memuse, size);
}

/**
 *  \brief   Function to map the protocol to the defined FLOW_PROTO_* enumeration.
 *
 *  \param   proto  protocol which is needed to be mapped
 */

uint8_t FlowGetProtoMapping(uint8_t proto)
{
    switch (proto) {
        case IPPROTO_TCP:
            return FLOW_PROTO_TCP;
        case IPPROTO_UDP:
            return FLOW_PROTO_UDP;
        case IPPROTO_ICMP:
            return FLOW_PROTO_ICMP;
        default:
            return FLOW_PROTO_DEFAULT;
    }
}

uint8_t FlowGetReverseProtoMapping(uint8_t rproto)
{
    switch (rproto) {
        case FLOW_PROTO_TCP:
            return IPPROTO_TCP;
        case FLOW_PROTO_UDP:
            return IPPROTO_UDP;
        case FLOW_PROTO_ICMP:
            return IPPROTO_ICMP;
        default:
            exit(EXIT_FAILURE);
    }
}

/* initialize the flow from the first packet
 * we see from it. */
void FlowInit(Flow *f, const Packet *p)
{
    SCEnter();
    SCLogDebug("flow %p", f);

    f->proto = p->proto;
    f->recursion_level = p->recursion_level;
    f->vlan_id[0] = p->vlan_id[0];
    f->vlan_id[1] = p->vlan_id[1];
    f->vlan_idx = p->vlan_idx;
    f->livedev = p->livedev;

    if (PKT_IS_IPV4(p)) {
        FLOW_SET_IPV4_SRC_ADDR_FROM_PACKET(p, &f->src);
        FLOW_SET_IPV4_DST_ADDR_FROM_PACKET(p, &f->dst);
        f->min_ttl_toserver = f->max_ttl_toserver = IPV4_GET_IPTTL((p));
        f->flags |= FLOW_IPV4;
    } else if (PKT_IS_IPV6(p)) {
        FLOW_SET_IPV6_SRC_ADDR_FROM_PACKET(p, &f->src);
        FLOW_SET_IPV6_DST_ADDR_FROM_PACKET(p, &f->dst);
        f->min_ttl_toserver = f->max_ttl_toserver = IPV6_GET_HLIM((p));
        f->flags |= FLOW_IPV6;
    } else {
        SCLogDebug("neither IPv4 or IPv6, weird");
        DEBUG_VALIDATE_BUG_ON(1);
    }

    if (p->tcph != NULL) { /* XXX MACRO */
        SET_TCP_SRC_PORT(p,&f->sp);
        SET_TCP_DST_PORT(p,&f->dp);
    } else if (p->udph != NULL) { /* XXX MACRO */
        SET_UDP_SRC_PORT(p,&f->sp);
        SET_UDP_DST_PORT(p,&f->dp);
    } else {
        /* nothing to do for this IP proto. */
        SCLogDebug("no special setup for IP proto %u", p->proto);
    }
    COPY_TIMESTAMP(&p->ts, &f->startts);

    f->protomap = FlowGetProtoMapping(f->proto);
    f->timeout_policy = FlowGetTimeoutPolicy(f);
    const uint32_t timeout_at = (uint32_t)f->startts.tv_sec + f->timeout_policy;
    f->timeout_at = timeout_at;

    if (MacSetFlowStorageEnabled()) {
        MacSet *ms = FlowGetStorageById(f, MacSetGetFlowStorageID());
        if (ms != NULL) {
            MacSetReset(ms);
        } else {
            ms = MacSetInit(10);
            FlowSetStorageById(f, MacSetGetFlowStorageID(), ms);
        }
    }

    SCReturn;
}

FlowStorageId g_bypass_info_id = { .id = -1 };

FlowStorageId GetFlowBypassInfoID(void)
{
    return g_bypass_info_id;
}

static void FlowBypassFree(void *x)
{
    FlowBypassInfo *fb = (FlowBypassInfo *) x;

    if (fb == NULL)
        return;

    if (fb->bypass_data && fb->BypassFree) {
        fb->BypassFree(fb->bypass_data);
    }
    SCFree(fb);
}

void RegisterFlowBypassInfo(void)
{
    g_bypass_info_id = FlowStorageRegister("bypass_counters", sizeof(void *),
                                              NULL, FlowBypassFree);
}
