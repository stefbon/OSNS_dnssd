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
#include "libosns-dnssd.h"

#include "osns/osns.h"
#include "osns/start.h"

static struct osns_ctx_action_s action;

#define NETWORK_RESOURCE_DOMAIN_FLAG_DNSSD              1
#define NETWORK_RESOURCE_HOST_FLAG_DNSSD                2
#define NETWORK_RESOURCE_SERVICE_FLAG_DNSSD             3
#define NETWORK_RESOURCE_TRANSPORT_FLAG_DNSSD           4
#define NETWORK_RESOURCE_ADDRESS_FLAG_DNSSD             5


static int NETWORK_db_add_network_domain(struct dstr_s *domain, uint64_t *p_dbid, unsigned int flags)
{
    logoutput_debug("%s: add domain %.*s", __FUNCTION__, domain->length, domain->str);
    return 0;
}

static int NETWORK_db_add_network_host(struct dstr_s *hostname, uint64_t dbid_domain, uint64_t *p_dbid, unsigned int flags)
{
    logoutput_debug("%s: add host %.*s", __FUNCTION__, hostname->length, hostname->str);
    return 0;
}

static unsigned char NETWORK_db_add_network_transport(struct dstr_s *transport, struct dstr_s *semantics, unsigned int portnr, uint64_t dbid_host, uint64_t *p_dbid, unsigned int flags)
{
    logoutput_debug("%s: add transport %.*s", __FUNCTION__, transport->length, transport->str);
    return 0;
}

static unsigned char NETWORK_db_add_network_service(struct dstr_s *service, uint64_t dbid_transport, uint64_t *p_dbid, unsigned int flags)
{
    logoutput_debug("%s: add service %.*s", __FUNCTION__, service->length, service->str);
    return 0;
}

static unsigned char NETWORK_db_add_network_address(struct ip_address_s *ip, uint64_t dbid_host, uint64_t *p_dbid, unsigned int flags)
{
    logoutput_debug("%s: add ip %s", __FUNCTION__, ip->ip);
    return 0;
}

/* check the service in dns sd style (_ftp, _http, _ssh ...) exists */

static unsigned char dnssd_service_check(struct dstr_s *dnssd_service, struct dstr_s *transport, struct dstr_s *service, unsigned int *p_portnr)
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

        if (transport) DSTR_set_str(transport, &tmp, 0);
        if (p_portnr) *p_portnr=portnr;
        return 1;

    } else {

        /* not found ... try there is a - in the service, like in sftp-ssh
            which is sftp over ssh ... so the service to connect to is ssh
            this is in general the case, also with for example https, this is
            the transport layer, and in most cases used for webpages (www),
            but it's also possible that it's providing the daap service,
            or printer service ipp */

        unsigned int length=DSTR_get_last_dstr(&tmp, '-', transport, 1, 0);

        if (length) {

            portnr=NETWORK_service_get_port_by_name(transport);

            if (portnr>=0) {

                if (service) DSTR_set_str(service, &tmp, 0);
                if (p_portnr) *p_portnr=portnr;
                return 1;

            }

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

static int dnssd_add_network_domain(struct dstr_s *domain, uint64_t *p_dbid)
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

    return NETWORK_db_add_network_domain(domain, p_dbid, NETWORK_RESOURCE_DOMAIN_FLAG_DNSSD);

}

static int dnssd_add_network_host(struct dstr_s *dnssd_hostname, uint64_t dbid_domain, uint64_t *p_dbid)
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

    return NETWORK_db_add_network_host(&hostname, dbid_domain, p_dbid, NETWORK_RESOURCE_HOST_FLAG_DNSSD);
}

static int dnssd_add_network_service(struct dstr_s *dnssd_service, struct dstr_s *dnssd_semantics, unsigned int portnr, uint64_t dbid_host, uint64_t *p_dbid)
{
    struct dstr_s transport=DSTR_INIT;
    struct dstr_s service=DSTR_INIT;
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

    servicecheck=dnssd_service_check(dnssd_service, &transport, &service, NULL);

    if (servicecheck==0) {

        logoutput_debug("%s: unable to determine the network service/port from service %.*s", __FUNCTION__, dnssd_service->length, dnssd_service->str);
        return -1;

    }

    /* get rid if the starting underscore */

    DSTR_set_str(&semantics, dnssd_semantics, 0);
    if ((semantics.str[0] == '_') && (semantics.length>0)) DSTR_shift(&semantics, 1);

    if (DSTR_get_length(&transport)) {

        if (NETWORK_db_add_network_transport(&transport, &semantics, portnr, dbid_host, p_dbid, NETWORK_RESOURCE_TRANSPORT_FLAG_DNSSD)==1) {

            /* is there a service found ? Like sftp ? */

            if (DSTR_get_length(&service)) {

                int tmp=NETWORK_db_add_network_service(&service, *p_dbid, NULL, NETWORK_RESOURCE_SERVICE_FLAG_DNSSD);

            }

            result=1;

        }

    }

    return result;

}

static int dnssd_add_network_address(struct io_addr_object_s *peer, uint64_t dbid_host, uint64_t *p_dbid)
{
    struct ip_address_s ip;

    memset(&ip, 0, sizeof(struct ip_address_s));

    if (IO_addr_object_get_network_address(peer, &ip)<1) {

        logoutput_debug("%s: unable to translate network address", __FUNCTION__);
        return -1;

    }

    return NETWORK_db_add_network_address(&ip, dbid_host, p_dbid, NETWORK_RESOURCE_ADDRESS_FLAG_DNSSD);
}

static void cb_errorclose_mdns_socket(struct mdns_socket_ctx_s *mctx)
{}

static unsigned char cb_select_mdns_socket(struct mdns_socket_ctx_s *mctx, char *name, unsigned int length)
{
    return 1; /* select everything */
}

static void cb_host_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct dstr_s *service, struct dstr_s *semantics, unsigned int ttl)
{

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;

        if (dnssd_add_network_domain(domain, &dbid_domain)==-1) return;
        if (dnssd_add_network_host(hostname, dbid_domain, &dbid_host)==-1) return;

    }

}

static void cb_service_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct dstr_s *service, struct dstr_s *semantics, unsigned int portnr)
{

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;
        uint64_t dbid_service=0;

        if (dnssd_add_network_domain(domain, &dbid_domain)==-1) return;
        if (dnssd_add_network_host(hostname, dbid_domain, &dbid_host)==-1) return;
        if (dnssd_add_network_service(service, semantics, portnr, dbid_host, &dbid_service)==-1) return;

    }

}

static void cb_addr_mdns_socket(struct mdns_socket_ctx_s *mctx, unsigned char action, struct io_addr_object_s *from, struct dstr_s *hostname, struct dstr_s *domain, struct io_addr_object_s *peer)
{

    if (action==MDNS_SOCKET_ACTION_ADD) {
        uint64_t dbid_domain=0;
        uint64_t dbid_host=0;
        uint64_t dbid_address=0;

        if (dnssd_add_network_domain(domain, &dbid_domain)==-1) return;
        if (dnssd_add_network_host(hostname, dbid_domain, &dbid_host)==-1) return;
        if (dnssd_add_network_address(peer, dbid_host, &dbid_address)==-1) return;

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
};

static int osns_manage_dnssd(struct osns_ctx_s *octx, unsigned char actioncode, struct osns_ctx_action_s *action)
{

    logoutput_debug("%s", __FUNCTION__);

    if (actioncode==OSNS_CTX_ACTION_CODE_DO) {

        DNSSD_init(&mdns_ctx);
        DNSSD_start();

    } else if (actioncode==OSNS_CTX_ACTION_CODE_UNDO) {

        DNSSD_finish();

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
