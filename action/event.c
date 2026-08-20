/* SPDX-License-Identifier: GLP-2.0-only */

#include "libosns-basic-system-headers.h"
#include "libosns-defaults.h"

#include "libosns-log.h"
#include "libosns-network.h"
#include "libosns-misc.h"
#include "libosns-list.h"
#include "libosns-datatypes.h"
#include "libosns-event.h"
#include "libosns-io.h"

#include "libosns-osns.h"

static struct osns_ctx_action_s event;
static struct osns_event_ctx_s event_ctx;

static int osns_manage_action_cb(struct osns_ctx_s *octx, unsigned char actioncode, struct osns_ctx_action_s *action)
{

    if (actioncode==OSNS_CTX_ACTION_CODE_DO) {

        OSNS_event_init_subscriptions(&event_ctx);
        octx->event_ctx=&event_ctx;

    } else if (actioncode==OSNS_CTX_ACTION_CODE_UNDO) {

        OSNS_event_clear_subscriptions(&event_ctx);
        octx->event_ctx=NULL;

    }

    return 1;
}

void OSNS_add_event_ctx_to_actions_list(struct osns_ctx_s *octx)
{

    OSNS_action_init(&event);

    event.name             = "event ctx";
    event.manage           = osns_manage_action_cb;

    OSNS_action_add(octx, &event);

}
