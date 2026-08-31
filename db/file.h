/* SPDX-License-Identifier: GLP-2.0-only */

#ifndef OSNS_DNSSD_DB_FILE_H
#define OSNS_DNSSD_DB_FILE_H

/* Prototypes */

unsigned char OSNS_fs_is_supported();

int OSNS_create_connection_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle);
void OSNS_release_connection_fs(struct osns_db_handle_s *handle);

int OSNS_search_db_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle, struct dstr_s *map, struct dstr_s *name);
int OSNS_modify_db_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle, char *attrtype, struct osns_value_s *value, unsigned int count, int modus, uint64_t parent_dbid, uint64_t *p_dbid, struct dstr_s *sub);

#endif
