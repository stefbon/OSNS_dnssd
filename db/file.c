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

static uint64_t dbid_host=0;
static uint64_t dbid_addr=0;
static uint64_t dbid_srv=0;
static unsigned int lock=0;

#define OSNS_DB_FS_LOCK_DBID_HOST		1
#define OSNS_DB_FS_LOCK_DBID_ADDR		2
#define OSNS_DB_FS_LOCK_DBID_SRV		4

/* NOTE:

    written with inspiration from:
    https://learning.oreilly.com/library/view/ldap-implementation-cookbook/0738413313/0738413313_ch08lev1sec2.html
    (registration required)

*/

unsigned char OSNS_fs_is_supported()
{
    return 1;
}

int OSNS_create_connection_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle)
{
    struct fs_object_s *fso=NULL;

    if (handle->type != OSNS_DB_TYPE_FS) return 0;

    fso=malloc(sizeof(struct fs_object_s));

    if (fso==NULL) {

	logoutput_debug("%s: not able to create fs object", __FUNCTION__);
	return -1;

    }

    FS_object_init(fso);

    /* open "root" of ddns sd fs tree */

    if (FS_open(NULL, 'p', &octx->db_ctx->db.root, fso, NULL, "directory,rdwr")==0) {

	logoutput_debug("%s: not able to open %s", __FUNCTION__, octx->db_ctx->db.root.start.length, octx->db_ctx->db.root.start.str);
	free(fso);
	return -1;

    }

    handle->db.fso=fso;
    return 1;

}

void OSNS_release_connection_fs(struct osns_db_handle_s *handle)
{

    if (handle->type == OSNS_DB_TYPE_FS) {

	if (handle->db.fso) {

	    FS_object_close(handle->db.fso);
	    FS_object_clear(handle->db.fso);
	    free(handle->db.fso);
	    handle->db.fso=NULL;

	}

    }

}

static int OSNS_db_walk(struct osns_db_handle_s *handle, struct dstr_s *map, unsigned char (* cb)(struct osns_db_handle_s *handle, struct fs_dentry_s *dentry, void *ptr), void *ptr)
{
    struct fs_object_s fso;
    struct fs_dentry_s dentry=FS_DENTRY_INIT;
    int result=0;
    int tmp=0;
    off_t off=0;

    logoutput_debug("%s", __FUNCTION__);

    FS_object_init(&fso);

    if (DSTR_get_length(map)) {

	tmp=FS_open(handle->db.fso, 'd', map, &fso, NULL, "directory");

    } else {
	struct osns_ctx_s *octx=handle->octx;

	tmp=FS_open(NULL, 'p', &octx->db_ctx->db.root, &fso, NULL, "directory,rdwr");

    }

    if (tmp<0) {

	logoutput_debug("%s: not able to open directory, error: %s", __FUNCTION__, strerror(errno));
	return -1;

    }

    readdentry:

    off=FS_readdentry(&fso, &dentry, 1);

    logoutput_debug("%s: off %lu  %.*s", __FUNCTION__, off, dentry.name.length, dentry.name.str);

    if (off>0) {

	if ((* cb)(handle, &dentry, ptr)) {

	    result=1;

	} else {

	    goto readdentry;

	}

    }

    FS_object_close(&fso);
    FS_object_clear(&fso);

    return result;

}

struct osns_db_find_dentry_hlpr_s {
    struct dstr_s			*name;
};

static unsigned char osns_db_find_dentry_cb(struct osns_db_handle_s *handle, struct fs_dentry_s *dentry, void *ptr)
{
    struct osns_db_find_dentry_hlpr_s *hlpr=(struct osns_db_find_dentry_hlpr_s *) ptr;

    return (DSTR_cmp_str(&dentry->name, hlpr->name, 1, 0, 0)) ? 1 : 0;
}

int OSNS_search_db_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle, struct dstr_s *map, struct dstr_s *name)
{
    struct osns_db_find_dentry_hlpr_s hlpr;

    hlpr.name=name;

    return OSNS_db_walk(handle, map, osns_db_find_dentry_cb, (void *) &hlpr);
}

static unsigned char osns_db_delete_dentry_cb(struct osns_db_handle_s *handle, struct fs_dentry_s *dentry, void *ptr)
{
    int tmp=FS_rm(handle->db.fso, 'd', &dentry->name);
    return 0;
}

static void OSNS_rmdir_contents(struct osns_db_handle_s *handle, const unsigned char type, void *ptr)
{
    struct dstr_s map=DSTR_INIT;

    if (FS_path_convert_type_ptr(type, ptr, &map)>0) {

	int result=OSNS_db_walk(handle, &map, osns_db_delete_dentry_cb, NULL);

    }

}

static void convert_osns_ptr_value(struct osns_value_s *value, struct dstr_s *data)
{

    switch (value->type) {

	case 'c':

	    data->str=(char *) value->ptr;
	    data->length=(data->str ? strlen(data->str) : 0);
	    break;

	case 'd':
	case 's':
	    struct dstr_s *other=(struct dstr_s *) value->ptr;

	    data->str=other->str;
	    data->length=other->length;
	    break;

	case 'i':
	    struct ip_address_s *ip=(struct ip_address_s *) value->ptr;

	    data->str=ip->ip;
	    data->length=ip->length;
	    break;

    }

}

static unsigned int OSNS_construct_name(char *attrtype, struct osns_value_s *value, unsigned int count, uint64_t dbid, char *buffer, unsigned int size)
{
    int tmp=0;

    if (strcmp(attrtype, "host")==0) {
	struct dstr_s data=DSTR_INIT;

	if (count<1) return 0;
	convert_osns_ptr_value(&value[0], &data);

	if (DSTR_get_length(&data)==0) return 0;

	if (buffer) {

	    tmp=snprintf(buffer, size, "host:%.8lu:%.*s", dbid, data.length, data.str);

	} else {

	    tmp=14 + data.length;

	}

    } else if ((strcmp(attrtype, "ipv4")==0) || (strcmp(attrtype, "ipv6")==0)) {
	struct dstr_s data=DSTR_INIT;

	if (count<1) return 0;
	convert_osns_ptr_value(&value[0], &data);

	if (DSTR_get_length(&data)==0) return 0;

	if (buffer) {

	    tmp=snprintf(buffer, size, "%s:%.*s", attrtype, data.length, data.str);

	} else {

	    tmp=strlen(attrtype) + 2 + data.length;

	}

    } else if (strcmp(attrtype, "srv")==0) {
	struct dstr_s data=DSTR_INIT;
	struct dstr_s port=DSTR_INIT;

	convert_osns_ptr_value(&value[0], &data);

	if (DSTR_get_length(&data)==0) return 0;
	if (count>=2) convert_osns_ptr_value(&value[1], &port);

	if (buffer) {

	    if (DSTR_get_length(&port)) {

		tmp=snprintf(buffer, size, "srv:%.*s:%.*s", data.length, data.str, port.length, port.str);

	    } else {

		tmp=snprintf(buffer, size, "srv:%.*s", data.length, data.str);

	    }

	} else {

	    tmp=5 + data.length + 1 + port.length;

	}

    }

    return tmp;

}

/* FIND a symlink to specific attrtype */

struct osns_db_find_symlink_hlpr_s {
    uint64_t			dbid;
    struct dstr_s		*map;
    char			*attrtype;
    struct osns_value_s		*value;
    unsigned int 		count;
};

static unsigned char osns_db_find_symlink_cb(struct osns_db_handle_s *handle, struct fs_dentry_s *dentry, void *ptr)
{
    struct osns_db_find_symlink_hlpr_s *hlpr=(struct osns_db_find_symlink_hlpr_s *) ptr;
    unsigned char tmp=0;
    unsigned char result=0;

    if (FS_dentry_is_symlink(dentry)) {
	struct fs_path_s path;
	struct dstr_s target=DSTR_INIT;
	struct dstr_s partstr=DSTR_INIT;
	struct dstr_s tmpstr=DSTR_INIT;

	logoutput_debug("%s: found symlink %.*s", __FUNCTION__, dentry->name.length, dentry->name.str);

	DSTR_set_str(&tmpstr, &dentry->name, 0);

	/* name of symlink is like attrtpe:dbid */

	if (DSTR_get_first_dstr(&tmpstr, ':', &partstr, 1, 0)==0) return 0; 
	if (DSTR_cmp_bytes(&partstr, hlpr->attrtype, 0, 1, 0, 0)==0) return 0;
	if (DSTR_get_first_dstr(&tmpstr, ':', &partstr, 1, 1)==0) return 0;  /* tmpstr now holds the second part of the name: dbid*/

	FS_path_init(&path);
	FS_path_append_init(&path, FS_PATH_FLAG_BUFFER_ALLOC);

	if (DSTR_get_length(hlpr->map)) {

	    tmp=FS_path_append(&path, 'd', hlpr->map, 0);
	    tmp=FS_path_append(&path, 'd', &dentry->name, 1);

	} else {

	    tmp=FS_path_append(&path, 'd', &dentry->name, 0);

	}

	logoutput_debug("%s: readlink %.*s", __FUNCTION__, path.start.length, path.start.str);

	if (FS_readlink(handle->db.fso, 'p', (void *) &path, &target, 0)>0) {

	    /* symlink points to the real record,
		first part of this record is exactly the same
		as the name of the symlink */

	    

	    // if (DSTR_cmp_str(&target, &dentry->name, 0, 0, 0)) {
		uint64_t dbid=DSTR_convert_str_to_long(&partstr);
		unsigned int length=OSNS_construct_name(hlpr->attrtype, hlpr->value, hlpr->count, dbid, NULL, 0);
		char name[length+1];

		length=OSNS_construct_name(hlpr->attrtype, hlpr->value, hlpr->count, dbid, name, length+1);

		logoutput_debug("%s: compare target %.*s with name %s", __FUNCTION__, target.length, target.str, name);

		if (DSTR_cmp_bytes(&target, name, 0, 1, 0, 0)) {

		    /* success*/
		    hlpr->dbid=dbid;
		    result=1;

		}

	    // }

	    DSTR_clear(&target);

	}

	FS_path_clear(&path);

    }

    return result;

}

static int OSNS_find_symlink(struct osns_db_handle_s *handle, struct dstr_s *map, char *attrtype, struct osns_value_s *value, unsigned int count, uint64_t *p_dbid)
{
    struct osns_db_find_symlink_hlpr_s hlpr;
    int result=0;

    hlpr.dbid=0;
    hlpr.map=map;
    hlpr.attrtype=attrtype;
    hlpr.value=value;
    hlpr.count=count;

    result=OSNS_db_walk(handle, map, osns_db_find_symlink_cb, (void *) &hlpr);
    if (result>0) *p_dbid=hlpr.dbid;

    return result;
}

/* FIND ipv4 and ipv6 field */

struct osns_db_find_field_hlpr_s {
    char			*attrtype;
    struct osns_value_s		*value;
    unsigned int 		count;
};

static unsigned char osns_db_find_field_cb(struct osns_db_handle_s *handle, struct fs_dentry_s *dentry, void *ptr)
{
    struct osns_db_find_field_hlpr_s *hlpr=(struct osns_db_find_field_hlpr_s *) ptr;
    unsigned char result=0;

    if (FS_dentry_is_file(dentry)) {

	/* filename shoudl be of form ipv4:192.168.2.1 or simular */

	unsigned int length=OSNS_construct_name(hlpr->attrtype, hlpr->value, hlpr->count, 0, NULL, 0);
	char name[length+1];

	length=OSNS_construct_name(hlpr->attrtype, hlpr->value, hlpr->count, 0, NULL, length+1);

	if (DSTR_cmp_bytes(&dentry->name, name, 0, 1, 0, 0)) {

	    result=1;

	}

    }

    return result;

}

static int OSNS_find_field(struct osns_db_handle_s *handle, struct dstr_s *map, char *attrtype, struct osns_value_s *value, unsigned int count)
{
    struct osns_db_find_field_hlpr_s hlpr;

    hlpr.attrtype=attrtype;
    hlpr.value=value;
    hlpr.count=count;

    return OSNS_db_walk(handle, map, osns_db_find_field_cb, (void *) &hlpr);

}

static unsigned char OSNS_db_find_host(struct osns_db_handle_s *handle, uint64_t dbid, struct dstr_s *target)
{
    unsigned int length=14;
    char name[length + 1];
    unsigned char result=0;

    /* look for the symlink */

    if (snprintf(name, length, "host:%.8lu", dbid)) {
	unsigned char tmp=0;

	if (FS_readlink(handle->db.fso, 'c', (void *) name, target, 0)>0) {

	    /* success */

	    result=1;

	}

    }

    return result;
}

static void OSNS_db_rm(struct osns_db_handle_s *handle, struct dstr_s *map, char *attrtype, uint64_t dbid, unsigned char isdir, unsigned char alsosymlink)
{
    unsigned int length=strlen(attrtype) + 12;
    char name[length];

    /* look for the symlink */

    if (snprintf(name, length, "%s:%.8lu", attrtype, dbid)>0) {
	struct fs_path_s symlink;
	struct dstr_s target=DSTR_INIT;
	unsigned char tmp=0;

	FS_path_init(&symlink);
	FS_path_append_init(&symlink, FS_PATH_FLAG_BUFFER_ALLOC);

	if (DSTR_get_length(map)) {

	    tmp=FS_path_append(&symlink, 'd', map, 0);
	    tmp=FS_path_append(&symlink, 'c', &name, 1);

	} else {

	    tmp=FS_path_append(&symlink, 'c', &name, 0);

	}

	if (FS_readlink(handle->db.fso, 'p', (void *) &symlink, &target, 0)>0) {
	    struct fs_path_s path;

	    FS_path_init(&path);
	    FS_path_append_init(&path, FS_PATH_FLAG_BUFFER_ALLOC);

	    if (DSTR_get_length(map)) {

		tmp=FS_path_append(&path, 'd', map, 0);
		tmp=FS_path_append(&path, 'd', &target, 1);

	    } else {

		tmp=FS_path_append(&path, 'd', &target, 0);

	    }

	    if (isdir) {

		OSNS_rmdir_contents(handle, 'p', &path);
		FS_rmdir(handle->db.fso, 'p', &path);

	    } else {

		FS_rm(handle->db.fso, 'p', &path);

	    }

	    FS_path_clear(&path);
	    DSTR_clear(&target);

	}

	if (alsosymlink) {

	    FS_rm(handle->db.fso, 'p', &symlink);

	}

	FS_path_clear(&symlink);

    }

}

static void OSNS_db_mk(struct osns_db_handle_s *handle, struct dstr_s *map, char *attrtype, char *name, uint64_t dbid, unsigned char isdir, unsigned char alsosymlink)
{
    struct fs_path_s path;
    int tmp=0;
    unsigned char result=0;

    FS_path_init(&path);
    FS_path_append_init(&path, FS_PATH_FLAG_BUFFER_ALLOC);

    if (DSTR_get_length(map)) {

	tmp=FS_path_append(&path, 'd', map, 0);
	tmp=FS_path_append(&path, 'c', name, 1);

    } else {

	tmp=FS_path_append(&path, 'c', name, 0);

    }

    if (isdir) {

	result=FS_mkdir(handle->db.fso, 'p', &path, NULL);

    } else {

	result=FS_mk(handle->db.fso, 'p', &path, NULL);

    }

    if (result) {

	logoutput_debug("%s: created %.*s", __FUNCTION__, path.start.length, path.start.str);

    } else {

	logoutput_debug("%s: unable to create %.*s", __FUNCTION__, path.start.length, path.start.str);
	goto out;

    }

    if (alsosymlink) {
	unsigned int length=strlen(attrtype) + 12;
	char sname[length + 1];

	if (snprintf(sname, length, "%s:%.8lu", attrtype, dbid)>0) {
	    struct fs_path_s symlink;
	    unsigned char tmp=0;
	    struct dstr_s target=DSTR_INIT;

	    FS_path_init(&symlink);
	    FS_path_append_init(&symlink, FS_PATH_FLAG_BUFFER_ALLOC);

	    if (DSTR_get_length(map)) {

		tmp=FS_path_append(&symlink, 'd', map, 0);
		tmp=FS_path_append(&symlink, 'c', sname, 1);

	    } else {

		tmp=FS_path_append(&symlink, 'c', sname, 0);

	    }

	    /* target it the path from above */

	    DSTR_set_bytes(&target, name, 0, 0);
	    logoutput_debug("%s: creating symlink %s to %.*s", __FUNCTION__, sname, target.length, target.str); 
	    FS_symlink(handle->db.fso, 'p', &symlink, &target);
	    FS_path_clear(&symlink);
	    DSTR_clear(&target);

	}

    }

    out:
    FS_path_clear(&path);

}

static void OSNS_db_write_service(struct osns_db_handle_s *handle, struct dstr_s *map, char *name, struct dstr_s *sub)
{
    struct fs_path_s path;
    struct fs_object_s fso;
    int tmp=0;
    unsigned int length=sub ? sub->length : 0;
    unsigned char result=0;
    char buffer[length + 2];

    if (length==0) return;
    memcpy(buffer, sub->str, length);
    buffer[length]=13;
    buffer[length+1]=10;

    FS_object_init(&fso);

    FS_path_init(&path);
    FS_path_append_init(&path, FS_PATH_FLAG_BUFFER_ALLOC);

    if (DSTR_get_length(map)) {

	tmp=FS_path_append(&path, 'd', map, 0);
	tmp=FS_path_append(&path, 'c', name, 1);

    } else {

	tmp=FS_path_append(&path, 'c', name, 0);

    }

    tmp=FS_open(handle->db.fso, 'p', &path, &fso, NULL, "rdwr,append");

    if (tmp<0) {

	logoutput_debug("%s: not able to open file %.*s, error: %s", __FUNCTION__, path.start.length, path.start.str, strerror(errno));
	return;

    }

    tmp=FS_pwrite(&fso, buffer, length+2, 0, "append");
    result=FS_close(&fso);

}

uint64_t OSNS_db_get_dbid(struct osns_ctx_s *octx, unsigned int what)
{
    uint64_t dbid=0;

    if (EVENT_signal_lock_flag(octx->esignal, &lock, what)) {

	if (what==OSNS_DB_FS_LOCK_DBID_HOST) {

	    dbid_host++;
	    dbid=dbid_host;

	} else if (what==OSNS_DB_FS_LOCK_DBID_ADDR) {

    	    dbid_addr++;
	    dbid=dbid_addr;

	} else if (what==OSNS_DB_FS_LOCK_DBID_SRV) {

    	    dbid_srv++;
	    dbid=dbid_srv;

	}

	EVENT_signal_unlock_flag(octx->esignal, &lock, what);

    }

    return dbid;


}

int OSNS_modify_db_fs(struct osns_ctx_s *octx, struct osns_db_handle_s *handle, char *attrtype, struct osns_value_s *value, unsigned int count, int modus, uint64_t parent_dbid, uint64_t *p_dbid, struct dstr_s *sub)
{
    int tmp=0;


    // } else if ((strcmp(attrtype, "ipv4")==0) || (strcmp(attrtype, "ipv6")==0) || (strcmp(attrtype, "srv")==0)) {

    if (modus<0) {

	if (strcmp(attrtype, "host")==0) {
	    struct dstr_s map=DSTR_INIT;
	    uint64_t dbid=0;

	    if (OSNS_find_symlink(handle, &map, attrtype, value, count, &dbid)>0) {

		OSNS_db_rm(handle, &map, attrtype, dbid, 1, 1);

	    }

	    return 1;

	} else {
	    struct dstr_s target=DSTR_INIT;

	    if (OSNS_db_find_host(handle, parent_dbid, &target)) {
		uint64_t dbid=0;

		/* delete */

		if (OSNS_find_symlink(handle, &target, attrtype, value, count, &dbid)>0) {

		    OSNS_db_rm(handle, &target, attrtype, dbid, 1, 1);

		}

	    }

	    DSTR_clear(&target);

	}

    } else if (modus>0) {

	/* insert  */

	if (strcmp(attrtype, "host")==0) {
	    struct dstr_s map=DSTR_INIT;
	    uint64_t dummyid=0;
	    int result=OSNS_find_symlink(handle, &map, attrtype, value, count, &dummyid);
	    unsigned char hostfound=(result>0 ? 1 : 0);

	    if (result<0) {

		logoutput_debug("%s: some error ... cannot continue", __FUNCTION__);
		return -1;

	    }

	    if (modus==OSNS_DNSSD_MODUS_INSERT_OR_IGNORE) {

		if (hostfound) {

		    /* found */

		    if (p_dbid) *p_dbid=dummyid;
		    logoutput_debug("%s: host already found (dbis=%lu)", __FUNCTION__, dummyid);
		    return 0;

		}

	    }

	    if (hostfound) {
		uint64_t dbid=0;
		struct dstr_s target=DSTR_INIT;

		/* found it: remove it */

		if (OSNS_find_symlink(handle, &target, attrtype, value, count, &dbid)>0) {

		    /* rm previous "field" with wrong address*/

		    OSNS_db_rm(handle, &target, attrtype, dbid, 1, 1);

		}

		DSTR_clear(&target);

	    }

	    /* create new */

	    {
		    uint64_t dbid=OSNS_db_get_dbid(octx, OSNS_DB_FS_LOCK_DBID_HOST);
		    unsigned int length=OSNS_construct_name(attrtype, value, count, dbid, NULL, 0);
		    char name[length+1];
		    struct dstr_s map=DSTR_INIT;

		    length=OSNS_construct_name(attrtype, value, count, dbid, name, length+1);
		    OSNS_db_mk(handle, &map, attrtype, name, dbid, 1, 1);

		    if (p_dbid) *p_dbid=dbid;

	    }


	} else if ((strcmp(attrtype, "ipv4")==0) || (strcmp(attrtype, "ipv6")==0) || (strcmp(attrtype, "srv")==0)) {
	    struct dstr_s target=DSTR_INIT;

	    /* dealing with a ipv4/ipv6 field: of both there is only one
		first find the host container */

	    if (OSNS_db_find_host(handle, parent_dbid, &target)>0) {

		if (OSNS_find_field(handle, &target, attrtype, value, count)==0) {

		    unsigned int length=OSNS_construct_name(attrtype, value, count, 0, NULL, 0);
		    char name[length+1];

		    length=OSNS_construct_name(attrtype, value, count, 0, name, length+1);
		    OSNS_db_mk(handle, &target, attrtype, name, 0, 0, 0);

		    if (sub) OSNS_db_write_service(handle, &target, name, sub);

		}

		DSTR_clear(&target);

	    } else {

		logoutput_debug("%s: some error ... host not found", __FUNCTION__);

	    }

	} else {

	    logoutput_debug("%s: some error ... attrtype %s not found", __FUNCTION__, attrtype);

	}

    }

    return tmp;
}
