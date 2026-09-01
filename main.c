/* SPDX-License-Identifier: GLP-2.0-only */

#include "libosns-basic-system-headers.h"
#include "libosns-defaults.h"

#include "libosns-log.h"

#include "libosns-datatypes.h"
#include "libosns-event.h"
#include "libosns-list.h"
#include "libosns-threads.h"
#include "libosns-eventloop.h"
#include "libosns-io.h"
#include "libosns-network.h"
#include "libosns-system.h"
#include "libosns-file.h"
#include "libosns-pid.h"
#include "libosns-fs.h"
#include "libosns-user.h"
#include "libosns-osns.h"

#include "osns/arguments.h"
#include "osns/config.h"
#include "osns/event.h"
#include "osns/start.h"

#include "action/dnssd.h"
#include "action/event.h"
#include "action/pidfile.h"

static struct dstr_s programname=DSTR_INIT;
struct osns_options_s options;

static void system_signal_create_osns_event(unsigned int signo, pid_t pid, union system_signal_type_u *type, void *ptr)
{
    struct osns_ctx_s *octx=(struct osns_ctx_s *) ptr;
    struct osns_event_s event;

    /* create an osns event for other subsystems to react on */

    memset(&event, 0, sizeof(struct osns_event_s));

    event.type=OSNS_EVENT_TYPE_SIGNAL;

    event.event.signal.code=signo;
    event.event.signal.pid=pid;
    event.event.signal.type=type;

    OSNS_event_process(octx, &event);

}

static void system_signal_process_event(unsigned int signo, pid_t pid, union system_signal_type_u *type, void *ptr)
{

    logoutput_debug("%s: received %u from pid %u", __FUNCTION__, signo, pid);

    switch (signo) {

	    case SIGHUP:
	    case SIGTERM:
	    case SIGINT:
	    case SIGSTOP:
	    case SIGABRT:
	    case SIGQUIT:

		struct osns_ctx_s *octx=(struct osns_ctx_s *) ptr;
		struct pid_info_s *pinfo=octx->pinfo;

		if (pinfo->pid != pid) {

	    	    logoutput_debug("%s: caught (HUP/TERM/INT/STOP/ABRT/QUIT) signal %i from pid %u", __FUNCTION__, signo, pid);
	    	    BEVENTLOOP_stop();
	    	    kill(pinfo->pid, SIGUSR2);

		}

	        break;

	    case SIGUSR1:
	    case SIGUSR2:

	        logoutput_debug("%s: caught USR1/2 signal %i from %u", __FUNCTION__, signo, pid);
	        system_signal_create_osns_event(signo, pid, type, ptr);
	        break;

	    case SIGIO:

	/*
	        TODO:
	        when receiving an SIGIO signal another application is trying to open a file
	        is this really the case?
	        then the fuse fs is the owner!?

	        note 	pid
			fd
	*/

	        logoutput_debug("%s: caught IO signal %u from pid %u for fd %u events %u", __FUNCTION__, signo, pid, type->io.fd, type->io.events);
	        system_signal_create_osns_event(signo, pid, type, ptr);
	        break;

	    case SIGCHLD:

                logoutput_debug("%s: caught CHLD signal %u pid %u code %u status %u", __FUNCTION__, signo, pid, type->chld.code, type->chld.status);
	        system_signal_create_osns_event(signo, pid, type, ptr);
	        break;

	    default:

	        logoutput_debug("%s: caught unsupported signal %i from pid %u", __FUNCTION__, signo, pid);

    }

}

static int OSNS_check_user_running(struct osns_ctx_s *octx)
{

    /* get pid */

    if (PID_get_info(0, (PID_INFO_MASK_PID | PID_INFO_MASK_UID | PID_INFO_MASK_GID), octx->pinfo)==0) {

        logoutput_warning("%s: unable to get process info like uid", __FUNCTION__);
        return -1;

    }

    if (USER_lookup_by_unique_uid(octx->user, octx->pinfo->uid)==0) {

        logoutput_warning("%s: unable to lookup user with uid %u", __FUNCTION__, octx->pinfo->uid);
        return -1;

    }

    /*
	check the combination between role and the user this program is running with match:
	- system can only be run as root
	- all other programss (client, app en hlpr ...) run as desktop user
    */

    if (octx->role==OSNS_CTX_ROLE_SYSTEM) {

        if (USER_is_root(octx->user)==0) {

            logoutput_warning("%s: error ... role is system, but running not as root", __FUNCTION__);
            return 0;

        }

    } else {

        if (USER_is_desktop_user(octx->user)==0) {

            logoutput_warning("%s: error ... not running as desktop user", __FUNCTION__);
            return 0;

        }

    }

    return 1;

}

int main(int argc, char *argv[])
{
    struct osns_arguments_s arguments=OSNS_ARGUMENT_INIT;
    struct fs_path_s programcmdline=FS_PATH_INIT;
    struct pid_info_s pinfo;
    struct user_s user;
    struct event_shared_signal_s *esignal=EVENT_signal_get_default();
    struct osns_ctx_s octx;
    unsigned char role=OSNS_CTX_ROLE_APP;
    int result=0;

    LOGGING_switch_backend("std");
    LOGGING_set_level(LOG_DEBUG);

    /* init */

    USER_get_system_range_uid();
    USER_init(&user);
    PID_info_init(&pinfo);
    FS_path_set_bytes(&programcmdline, argv[0], 0, 0, 0);

    /* get executable/programname from cmdline */

    FS_path_get_filename(&programcmdline, &programname, 0);
    logoutput_debug("%s: started cmd %s name %.*s", __FUNCTION__, argv[0], programname.length, programname.str);

    /* arguments */

    result=OSNS_parse_arguments(argc, argv, &arguments, (OSNS_ARGUMENT_HELP | OSNS_ARGUMENT_PPID | OSNS_ARGUMENT_PIDFILE));

    if (result==-1) {

        logoutput_debug("%s: error parsing arguments ... cannot continue", __FUNCTION__);
        exit(-1);

    }

    if (arguments.flags & OSNS_ARGUMENT_HELP) {

        logoutput_debug("%s: %s [--help] [--ppid pid] [--pidfile]", __FUNCTION__, argv[0]);
        exit(0);

    }

    LOGGING_switch_backend("syslog");
    LOGGING_set_level(LOG_DEBUG);

    /* initialize the osns ctx (which is the backbone of all osns programs) */

    OSNS_ctx_init(&octx, role, esignal, &options, &user, &pinfo, &programname, &arguments);

    /* read config options */

    OSNS_config_init(&options, role);
    OSNS_config_read_config(&user, &options, role);

    /* fork to daemonize */

    result=SYSTEM_fork((SYSTEM_FORK_FLAG_REDIRECT_STD | SYSTEM_FORK_FLAG_CHDIR_ROOT | SYSTEM_FORK_FLAG_SET_SESSION_ID), NULL, 0);

    if (result==-1) {

	logoutput_warning("%s: cannot continue ... unable to fork", __FUNCTION__);
        goto exitconfig;

    } else if (result>0) {

	logoutput_warning("%s: forked to %i", __FUNCTION__, result);
	goto exitconfig;

    }

    /* get the executable and other process info
        (after forking -> pid is not the same anymore) */

    if (PID_get_info(0, (PID_INFO_MASK_PID | PID_INFO_MASK_EXECUTABLE), &pinfo)==0) {

        logoutput_warning("%s: unable to get process info like executable", __FUNCTION__);
        return -1;

    }

    logoutput_debug("%s: daemonized, found exe %.*s with pid %u", __FUNCTION__, pinfo.executable.start.length, pinfo.executable.start.str, pinfo.pid);

    if (FS_path_get_filename(&pinfo.executable, &programname, 0)) {

        logoutput_debug("%s: program name %.*s", __FUNCTION__, programname.length, programname.str);

    } else {

        logoutput_debug("%s: program name not found ... cannot continue", __FUNCTION__);
        return -1;

    }

    /* initialize eventloop */

    BEVENTLOOP_init();

    /* SIGNAL monitor */

    if (SYSTEM_signal_monitor_start(NULL, esignal, system_signal_process_event, &octx)==1) {

        logoutput_debug("%s: signal handler started", __FUNCTION__);

    } else {

        logoutput_debug("%s: signal handler not started", __FUNCTION__);

    }

    /* build list with actions */

    OSNS_add_dnssd_to_actions_list(&octx);
    OSNS_add_event_ctx_to_actions_list(&octx);
    OSNS_add_pidfile_to_actions_list(&octx);

    /* Initialize and start default threads
	NOTE: important to start these after initializing the signal handler,
	if not doing this this way any signal will make the program crash
	maybe change this by having a shared init set */

    LOCAL_threads_init();
    LOCAL_threads_set_maxnr(options.maxthreads.value);
    LOCAL_threads_start();

    OSNS_action_process_all_start(&octx);

    BEVENTLOOP_start();

    out:
    outfinal:

    OSNS_action_process_all_undo(&octx);

    SYSTEM_signal_monitor_stop();
    BEVENTLOOP_clear();
    LOCAL_threads_stop();

    exitconfig:

    OSNS_config_free_options(&options, role);
    USER_clear(&user);
    return 0;

}
