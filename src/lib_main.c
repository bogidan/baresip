
#ifdef SOLARIS
#define __EXTENSIONS__ 1
#endif
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT
#include <getopt.h>
#endif
#include <re.h>
#include <baresip.h>
#include "core.h"

#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>

#define lengthOf(a) (sizeof(a)/sizeof(*a))

// Variables
//==============================================================================

// Function Definitions
//==============================================================================

static int conf_modules(void);

static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		mod_close();
		exit(0);
	}

	term = true;

	info("terminated by signal %d\n", sig);

	ua_stop_all(false);
}

#include "../modules/integration/integration.h"
struct control_st {
	struct mqueue *mq;
};

static void mqueue_handler(int id, void *data, void *arg)
{
	struct baresip_control *control = arg;
	struct control_st *st = control->ctx;
	int err = 0;

	switch(id) {
	case eQuit:
		re_printf("Quit\n");
		ua_stop_all(false);
		break;
	case eRegister:
		re_printf("Register Account\n");
		err = ua_alloc(NULL, (char*)data);
		break;
	case eAnswer:
		re_printf("Answer Call\n");
		ua_hold_answer(uag_current(), NULL);
		break;
	case eHangup:
		re_printf("Hang-Up Call\n");
		ua_hangup(uag_current(), NULL, 0, NULL);
		break;
	case eDial:
		re_printf("Dial Call (%s)\n", (char*)data);
		err = ua_connect(uag_current(), NULL, NULL, (char*)data, NULL, VIDMODE_OFF);
		break;
	}

	// Echo back
	if( control->cb == NULL ) return;
	control->cb(control->arg, id, (void*)err);
}

int lib_control(void* ctx, enum baresip_et id, void* data)
{
	if( ctx == NULL ) return -1;
	return mqueue_push( ((struct control_st*)ctx)->mq, id, data);
}

void ua_event_handler(struct ua *ua, enum ua_event ev, struct call *call, const char *prm, void *arg)
{
	struct baresip_control *control = arg;
	if( control->cb == NULL ) return;

	control->cb(control->arg, 0, NULL);
}

static void destructor(void *arg)
{
	struct control_st *st = arg;
	mem_deref(st->mq);
}

static int init_control( struct baresip_control *control )
{
	struct control_st *st;
	int err = 0;

	// init control struct
	st = mem_zalloc(sizeof(*st), destructor);
	if (!st) return ENOMEM;

	// init message queue
	err = mqueue_alloc(&st->mq, mqueue_handler, st);
	if (err) return -1;

	control->ctx = (void*) st;
	return 0;
}


int lib_main( struct baresip_control *control )
{
	bool prefer_ipv6 = false, run_daemon = false;
	const char *ua_eprm = NULL;
	const char *sw_version = "baresip v" BARESIP_VERSION " (" ARCH "/" OS ")";
	int err;

	(void)sys_coredump_set(true);

	err = libre_init();
	if(err) goto out;

	// TODO: setup correct calling of
	// conf_configure()
	// struct config *conf_config(void)

	err = lib_config_parse_conf(); // lib_conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	// Initialise User Agents
	err = ua_init(sw_version, true, true, true, prefer_ipv6);
	if (err) goto out;

	if (ua_eprm) {
		err = uag_set_extra_params(ua_eprm);
		if (err) goto out;
	}

	// Register event handler
	err = uag_event_register(ua_event_handler, control);
	if (err) goto out;

	// Load modules
	err = conf_modules();
	if (err) goto out;

	// Launch as deamon
	if (run_daemon) {
		err = sys_daemon();
		if (err) goto out;

		log_enable_stderr(false);
	}

	// Init control message queue
	err = init_control( control );
	if (err) goto out;

	// Signal Init complete
	if( control->cb ) control->cb(control->arg, esInitialized, NULL);
	info("baresip is ready.\n");

	// Main Loop
	err = re_main(signal_handler);

	// Signal Closing
	if( control->cb ) control->cb(control->arg, esStopped, NULL);
out:
	if (err) ua_stop_all(true);

	ua_close();
	conf_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}

static int conf_modules(void)
{
	int err;

	err = lib_module_init();
	if (err) {
		warning("conf: configure module parse error (%m)\n", err);
		goto out;
	}

	//print_populated("audio codec",  list_count(aucodec_list()));
	//print_populated("audio filter", list_count(aufilt_list()));
#ifdef USE_VIDEO
	//print_populated("video codec",  list_count(vidcodec_list()));
	//print_populated("video filter", list_count(vidfilt_list()));
#endif

 out:
	return err;
}

