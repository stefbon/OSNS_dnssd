/* SPDX-License-Identifier: GLP-2.0-only */

#include "libosns-basic-system-headers.h"
#include "libosns-defaults.h"

#include "libosns-log.h"
#include "libosns-network.h"
#include "libosns-misc.h"
#include "libosns-list.h"
#include "libosns-datatypes.h"
#include "libosns-event.h"
#include "libosns-path.h"
#include "libosns-fs.h"

#include "libosns-osns.h"

#include "file.h"

static struct osns_db_handle_s *OSNS_db_create_handle(struct osns_ctx_s *octx)
{
    struct osns_db_ctx_s *db_ctx=octx->db_ctx;
    struct osns_db_handle_s *handle=malloc(sizeof(struct osns_db_handle_s));

    if (handle) {

	memset(handle, 0, sizeof(struct osns_db_handle_s));

	handle->type=db_ctx->type;
	LIST_element_init(&handle->list, NULL);
	handle->octx=octx;

	if (db_ctx->type==OSNS_DB_TYPE_FS) {

	    if (OSNS_create_connection_fs(octx, handle)==-1) {

		free(handle);
		return NULL;

	    }

	}

    }

    return handle;

}

struct osns_db_handle_s *OSNS_db_get_handle(struct osns_ctx_s *octx)
{
    struct osns_db_ctx_s *db_ctx=octx->db_ctx;
    struct osns_db_handle_s *handle=NULL;

    if (db_ctx==NULL) {

	logoutput_warning("%s: error ... db ctx not set", __FUNCTION__);
	return NULL;

    }

    if (EVENT_signal_lock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES)) {
	struct list_element_s *list=LIST_header_remove_first(&db_ctx->handles);

	if (list) {

	    handle=(struct osns_db_handle_s *)((char *)list - offsetof(struct osns_db_handle_s, list));

	} else {

	    handle=OSNS_db_create_handle(octx);

	}

	EVENT_signal_unlock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES);

    }

    return handle;
}

void OSNS_db_release_handle(struct osns_ctx_s *octx, struct osns_db_handle_s *handle)
{
    struct osns_db_ctx_s *db_ctx=octx->db_ctx;

    if (handle==NULL) return;

    if (EVENT_signal_lock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES)) {

	LIST_header_add_last(&db_ctx->handles, &handle->list);

	EVENT_signal_unlock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES);

    }

}

void OSNS_db_clear_handles(struct osns_ctx_s *octx)
{
    struct osns_db_ctx_s *db_ctx=octx->db_ctx;

    if (db_ctx==NULL) {

	logoutput_warning("%s: error ... db ctx not set", __FUNCTION__);
	return;

    }

    if (EVENT_signal_lock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES)) {
	struct list_element_s *list=LIST_header_remove_first(&db_ctx->handles);

	while (list) {
	    struct osns_db_handle_s *handle=(struct osns_db_handle_s *)((char *)list - offsetof(struct osns_db_handle_s, list));

	    if (handle->type==OSNS_DB_TYPE_FS) {

		OSNS_release_connection_fs(handle);

	    }

	    free(handle);
	    list=LIST_header_remove_first(&db_ctx->handles);

	}

	EVENT_signal_unlock_flag(octx->esignal, &db_ctx->lock, OSNS_DB_CTX_LOCK_HANDLES);

    }

}
