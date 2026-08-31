/* SPDX-License-Identifier: GLP-2.0-only */

#ifndef OSNS_DNSSD_DB_HANDLE_H
#define OSNS_DNSSD_DB_HANDLE_H

/* Prototypes */

struct osns_db_handle_s *OSNS_db_get_handle(struct osns_ctx_s *octx);
void OSNS_db_release_handle(struct osns_ctx_s *octx, struct osns_db_handle_s *handle);
void OSNS_db_clear_handles(struct osns_ctx_s *octx);

#endif
