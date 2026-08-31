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
#include "libosns-dnssd.h"

#include "osns/osns.h"
#include "osns/start.h"

#include "db/file.h"
#include "db/handle.h"

static struct osns_ctx_action_s action;
static struct osns_db_ctx_s db_ctx;

#define NETWORK_RESOURCE_DOMAIN_FLAG_DNSSD              1
#define NETWORK_RESOURCE_HOST_FLAG_DNSSD                2
#define NETWORK_RESOURCE_SERVICE_FLAG_DNSSD             3
#define NETWORK_RESOURCE_TRANSPORT_FLAG_DNSSD           4
#define NETWORK_RESOURCE_ADDRESS_FLAG_DNSSD             5

static int NETWORK_db_add_network_domain(struct osns_ctx_s *octx, struct dstr_s *domain, uint64_t *p_dbid, unsigned int flags, void *ptr)
{
    logoutput_debug("%s: add domain %.*s", __FUNCTION__, domain->length, domain->str);
    return 0;
}

static int NETWORK_db_add_network_host(struct osns_ctx_s *octx, struct dstr_s *hostname, uint64_t dbid_domain, uint64_t *p_dbid, unsigned int flags, void *ptr)
{
    struct osns_db_handle_s *handle=NULL;
    int result=0;

    logoutput_debug("%s: add host %.*s", __FUNCTION__, hostname->length, hostname->str);

    handle=OSNS_db_get_handle(octx);

    if (handle) {
	struct osns_value_s value[2];

	value[0].type='d';
	value[0].ptr=(void *)hostname;
	value[1].type=0;
	value[1].ptr=NULL;

	result=OSNS_modify_db_fs(octx, handle, "host", value, 2, OSNS_DNSSD_MODUS_INSERT_OR_IGNORE, dbid_domain, p_dbid, NULL); 
	OSNS_db_release_handle(octx, handle);

    }

    return result;
}

static unsigned char NETWORK_db_add_network_transport(struct osns_ctx_s *octx, struct dstr_s *transport, struct dstr_s *semantics, unsigned int portnr, uint64_t dbid_host, uint64_t *p_dbid, unsigned int flags, void *ptr)
{
    logoutput_debug("%s: add transport %.*s", __FUNCTION__, transport->length, transport->str);
    return 1;
}

static unsigned char NETWORK_db_add_network_service(struct osns_ctx_s *octx, struct dstr_s *service, uint64_t dbid_host, uint64_t *p_dbid, unsigned int flags, struct dstr_s *sub, void *ptr)
{
    struct osns_db_handle_s *handle=NULL;
    int result=0;

    logoutput_debug("%s: add service %.*s", __FUNCTION__, service->length, service->str);

    handle=OSNS_db_get_handle(octx);

    if (handle) {
	struct osns_value_s value[2];

	value[0].type='d';
	value[0].ptr=(void *)service;
	value[1].type=0;
	value[1].ptr=NULL;

	result=OSNS_modify_db_fs(octx, handle, "srv", value, 2, OSNS_DNSSD_MODUS_INSERT_OR_IGNORE, dbid_host, p_dbid, sub); 
	OSNS_db_release_handle(octx, handle);

    }

    return result;
}

static unsigned char NETWORK_db_add_network_address(struct osns_ctx_s *octx, struct ip_address_s *ip, uint64_t dbid_host, uint64_t *p_dbid, unsigned int flags, void *ptr)
{
    struct osns_db_handle_s *handle=NULL;
    int result=0;

    logoutput_debug("%s: add ip %s", __FUNCTION__, ip->ip);

    handle=OSNS_db_get_handle(octx);

    if (handle) {
	struct osns_value_s value[2];

	value[0].type='i';
	value[0].ptr=(void *)ip;
	value[1].type=0;
	value[1].ptr=NULL;

	if (ip->family==IP_ADDRESS_FAMILY_IPv4) {

	    result=OSNS_modify_db_fs(octx, handle, "ipv4", value, 2, OSNS_DNSSD_MODUS_INSERT_OR_IGNORE, dbid_host, p_dbid, NULL); 

	} else if (ip->family==IP_ADDRESS_FAMILY_IPv6) {

	    result=OSNS_modify_db_fs(octx, handle, "ipv6", value, 2, OSNS_DNSSD_MODUS_INSERT_OR_IGNORE, dbid_host, p_dbid, NULL); 

	}

	OSNS_db_release_handle(octx, handle);

    }

    return result;
}

/* check the service in dns sd style (_ftp, _http, _ssh ...) exists */

static unsigned char dnssd_service_check(struct dstr_s *dnssd_service, struct dstr_s *service, struct dstr_s *sub, unsigned int *p_portnr)
{
    int portnr=0;
    struct dstr_s tmp=DSTR_INIT;

    logoutput_debug("%s: check service %.*s", __FUNCTION__, dnssd_service->length, dnssd_service->str);

    /* convert the dns sd style to a "normal" name by skipping the starting underscore */

    DSTR_set_str(&tmp, dnssd_service, 0);
    if ((tmp.str[0] == '_') && (tmp.length>0)) DSTR_shift(&tmp, 1);

    /* check the service is known to the system -> if so portnr is non negative */

    portnr=NETWORK_service_get_port_by_name(&tmp);

    if (portnr>=0) {

        if (service) DSTR_set_str(service, &tmp, 0);
        if (p_portnr) *p_portnr=portnr;
        return 1;

    } else {
	struct dstr_s dummy=DSTR_INIT;

        /* not found ... try there is a - in the service, like in sftp-ssh
            which is sftp over ssh ... so the service to connect to is ssh
            this is in general the case, also with for example https, this is
            the transport layer, and in most cases used for webpages (www),
            but it's also possible that it's providing the daap service,
            or printer service ipp */

	if (sub==NULL) sub=&dummy;

        unsigned int length=DSTR_get_first_dstr(&tmp, '-', sub, 1, 0);

        if (length) {

            portnr=NETWORK_service_get_port_by_name(sub);

            if (portnr>=0) {

                if (p_portnr) *p_portnr=portnr;
                return 1;

            }

	    if (service) DSTR_set_str(service, &tmp, 0);

        }

    }

    return 0;
}

static void dnssd_shift_starting_spaces(struct dstr_s *stra)
{
    while (stra->length && TXT_isspace(stra->str[0])) DSTR_shift_raw(stra, 1);
}

static void dnssd_remove_trailing_spaces(struct dstr_s *stra)
{
    while (stra->length && TXT_isspace(stra->str[stra->length - 1])) stra->length--;
}

static int dnssd_add_network_domain(struct osns_ctx_s *octx, struct dstr_s *domain, uint64_t *p_dbid, void *ptr)
{
    unsigned int length=0;

    if (DSTR_is_empty(domain)) return 0;

    if (TXT_replace_char(domain->str, domain->length, (REPLACE_CNTRL_FLAG_TEXT | REPLACE_CNTRL_FLAG_BINARY), NULL)>0) return -1;
    dnssd_shift_starting_spaces(domain);
    dnssd_remove_trailing_spaces(domain);

    length=DSTR_get_length(domain);

    if ((length==0) || (length > (NETWORK_HOSTNAME_FQDN_MAX_LENGTH - NETWORK_HOSTNAME_MAX_LENGTH))) {

        logoutput_debug("%s: length (%u) of domainname zero or too big", __FUNCTION__, length);
        return -1;

    }

    return NETWORK_db_add_network_domain(octx, domain, p_dbid, NETWORK_RESOURCE_DOMAIN_FLAG_DNSSD, ptr);

}

static int dnssd_add_network_host(struct osns_ctx_s *octx, struct dstr_s *dnssd_hostname, uint64_t dbid_domain, uint64_t *p_dbid, void *ptr)
{
    struct dstr_s hostname=DSTR_INIT;
    unsigned int length=0;

    if (DSTR_is_empty(dnssd_hostname)) {

        logoutput_debug("%s: hostname empty ... cannot continue", __FUNCTION__);
        return -1;

    }

    /* sometimes a name is added -> before <- the hostname using the '@' sign */

    unsigned int tmp=DSTR_get_last_dstr(dnssd_hostname, '@', &hostname, 1, 1);

    if (tmp) 
        logoutput_debug("%s: found a @ in the DNSSD hostname, skipping %.*s and taking %.*s", __FUNCTION__, dnssd_hostname->length, dnssd_hostname->str, hostname.length, hostname.str);

    /* remove any starting and trailing spaces
        should there also be a check on ctrl/non-printable characters ??? TODO .... */

    if (TXT_replace_char(hostname.str, hostname.length, (REPLACE_CNTRL_FLAG_TEXT | REPLACE_CNTRL_FLAG_BINARY), NULL)>0) return -1;
    dnssd_shift_starting_spaces(&hostname);
    dnssd_remove_trailing_spaces(&hostname);

    length=DSTR_get_length(&hostname);

    if ((length > NETWORK_HOSTNAME_MAX_LENGTH) || (length==0)) {

        logoutput_debug("%s: length (%u) of hostname invalid (zero or too big)", __FUNCTION__, length);
        return -1;

    }

    return NETWORK_db_add_network_host(octx, &hostname, dbid_domain, p_dbid, NETWORK_RESOURCE_HOST_FLAG_DNSSD, ptr);
}

static int dnssd_add_network_service(struct osns_ctx_s *octx, struct dstr_s *dnssd_service, struct dstr_s *dnssd_semantics, unsigned int portnr, uint64_t dbid_host, uint64_t *p_dbid, void *ptr)
{
    struct dstr_s service=DSTR_INIT;
    struct dstr_s sub=DSTR_INIT;
    struct dstr_s semantics=DSTR_INIT;
    unsigned char servicecheck=0;
    int result=0;

    if (DSTR_is_empty(dnssd_service)) {

        logoutput_debug("%s: service empty ... cannot continue", __FUNCTION__);
        return -1;

    }

    if (DSTR_is_empty(dnssd_semantics)) {

        logoutput_debug("%s: semantics empty ... cannot continue", __FUNCTION__);
        return -1;

    }

    if (TXT_replace_char(dnssd_service->str, dnssd_service->length, (REPLACE_CNTRL_FLAG_TEXT | REPLACE_CNTRL_FLAG_BINARY), NULL)>0) return -1;
    if (TXT_replace_char(dnssd_semantics->str, dnssd_semantics->length, (REPLACE_CNTRL_FLAG_TEXT | REPLACE_CNTRL_FLAG_BINARY), NULL)>0) return -1;

    servicecheck=dnssd_service_check(dnssd_service, &service, &sub, NULL);

    if (servicecheck==0) {

        logoutput_debug("%s: unable to determine the network service/port from service %.*s", __FUNCTION__, dnssd_service->length, dnssd_service->str);
        return -1;

    }

    /* get rid if the starting underscore */

    DSTR_set_str(&semantics, dnssd_semantics, 0);
    if ((semantics.str[0] == '_') && (semantics.length>0)) DSTR_shift(&semantics, 1);

    if (DSTR_get_length(&service)) {

        if (NETWORK_db_add_network_service(octx, &service, dbid_host, p_dbid, NETWORK_RESOURCE_SERVICE_FLAG_DNSSD, &sub, ptr)==1) {

            result=1;

        }

    }

    return result;

}

static int dnssd_add_network_address(struct osns_ctx_s *octx, struct io_addr_object_s *peer, uint64_t dbid_host, uint64_t *p_dbid, void *ptr)
{
    struct ip_address_s ip;

    memset(&ip, 0, sizeof(struct ip_address_s));

    if (IO_addr_object_get_network_address(peer, &ip)<1) {

        logoutput_debug("%s: unable to translate network address", __FUNCTION__);
        return -1;

    }

    return NETWORK_db_add_network_address(octx, &ip, dbid_host, p_dbid, NETWORK_RESOURCE_ADDRESS_FLAG_DNSSD, ptr);
}

/* MDSN CTX db's */

static void cb_errorclose_mdns_socket(struct mdns_socket_ctx_s *mctx)
{}

static unsigned char cb_select_mdns_socket(struct mdns_socket_ctx_s *mctx, char *name, unsigned int length)
{
    return 1; /* select everything */
}

static void cb_host_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct dstr_s *service, struct dstr_s *semantics, unsigned int ttl)
{
    struct osns_ctx_s *octx=(struct osns_ctx_s *) mctx->ptr;

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;

        if (dnssd_add_network_domain(octx, domain, &dbid_domain, mctx->ptr)==-1) return;
        if (dnssd_add_network_host(octx, hostname, dbid_domain, &dbid_host, mctx->ptr)==-1) return;

    }

}

static void cb_service_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct dstr_s *service, struct dstr_s *semantics, unsigned int portnr)
{
    struct osns_ctx_s *octx=(struct osns_ctx_s *) mctx->ptr;

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;
        uint64_t dbid_service=0;

        if (dnssd_add_network_domain(octx, domain, &dbid_domain, mctx->ptr)==-1) return;
        if (dnssd_add_network_host(octx, hostname, dbid_domain, &dbid_host, mctx->ptr)==-1) return;
        if (dnssd_add_network_service(octx, service, semantics, portnr, dbid_host, &dbid_service, mctx->ptr)==-1) return;

    }

}

static void cb_addr_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct io_addr_object_s *peer)
{
    struct osns_ctx_s *octx=(struct osns_ctx_s *) mctx->ptr;

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;
        uint64_t dbid_address=0;

        if (dnssd_add_network_domain(octx, domain, &dbid_domain, mctx->ptr)==-1) return;
        if (dnssd_add_network_host(octx, hostname, dbid_domain, &dbid_host, mctx->ptr)==-1) return;
        if (dnssd_add_network_address(octx, peer, dbid_host, &dbid_address, mctx->ptr)==-1) return;

    }

}

static struct mdns_socket_ctx_s mdns_ctx = {
    .esignal            = NULL,
    .cb_close           = cb_errorclose_mdns_socket,
    .cb_error           = cb_errorclose_mdns_socket,
    .cb_select          = cb_select_mdns_socket,
    .cb_host            = cb_host_mdns_socket,
    .cb_service         = cb_service_mdns_socket,
    .cb_addr            = cb_addr_mdns_socket,
    .ptr		= NULL,
};

static void OSNS_open_db_fs(struct osns_ctx_s *octx, struct osns_db_ctx_s *db_ctx)
{
    struct fs_path_s *path=&db_ctx->db.root;

    FS_path_init(path);
    FS_path_append_init(path, FS_PATH_FLAG_BUFFER_ALLOC);

    /* use the path for libexec modules */

    if (FS_path_append(path, 'p', (void *) &octx->options->runpath.value, 0)==0) return;
    if (FS_path_append(path, 'c', (void *) "dnssd", 1)==0) return;

    if (FS_mkdir(NULL, 'p', path, NULL)) {

	logoutput_debug("%s: created %.*s", __FUNCTION__, path->start.length, path->start.str);

    } else {

	logoutput_debug("%s: unable to create %.*s", __FUNCTION__, path->start.length, path->start.str);

    }

    if (FS_path_append(path, 'c', (void *) "db", 1)==0) return;

    if (FS_mkdir(NULL, 'p', path, NULL)) {

	logoutput_debug("%s: created %.*s", __FUNCTION__, path->start.length, path->start.str);

    } else {

	logoutput_debug("%s: unable to create %.*s", __FUNCTION__, path->start.length, path->start.str);

    }

    octx->db_ctx=db_ctx;

}

static void OSNS_close_db_fs(struct osns_ctx_s *octx, struct osns_db_ctx_s *dbctx)
{
    struct fs_path_s *path=&dbctx->db.root;

    FS_path_clear(path);
}

static int osns_manage_dnssd(struct osns_ctx_s *octx, unsigned char actioncode, struct osns_ctx_action_s *action)
{

    logoutput_debug("%s", __FUNCTION__);

    if (actioncode==OSNS_CTX_ACTION_CODE_DO) {

	/* use fs backend */

	db_ctx.type=OSNS_DB_TYPE_FS;
	LIST_header_init(&db_ctx.handles, 0);
	OSNS_open_db_fs(octx, &db_ctx);

	mdns_ctx.ptr=(void *) octx;

        DNSSD_init(&mdns_ctx);
        DNSSD_start();

    } else if (actioncode==OSNS_CTX_ACTION_CODE_UNDO) {

        DNSSD_finish();
        OSNS_db_clear_handles(octx);
        OSNS_close_db_fs(octx, &db_ctx);

    }

    return 1;
}

void OSNS_add_dnssd_to_actions_list(struct osns_ctx_s *octx)
{

    OSNS_action_init(&action);

    action.name                  = "dns sd";
    action.manage                = osns_manage_dnssd;

    OSNS_action_add(octx, &action);
}
