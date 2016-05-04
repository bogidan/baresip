
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

enum control_et {
	eQuit, eRegister,
};

struct control_st {
	struct mqueue *mq;
};

static void destructor(void *arg)
{
	struct control_st *st = arg;
	mem_deref(st->mq);
}

static void mqueue_handler(int id, void *data, void *arg)
{
	struct control_st *st = arg;
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
	}
	//tmr_start(&st->tmr, RELEASE_VAL, timeout, st);
	//report_key(st, id);
	// mqueue_push(st->mq, ch, 0);
}

static int lib_init_control( void** ctx )
{
	struct control_st *st;
	int err = 0;

	// init control struct
	st = mem_zalloc(sizeof(*st), destructor);
	if (!st) return ENOMEM;

	// init message queue
	err = mqueue_alloc(&st->mq, mqueue_handler, st);
	if (err) return -1;

	*ctx = (void*) st;
	return 0;
}

static int push(void* ctx, int id, void* data) {
	return mqueue_push( ((struct control_st*)ctx)->mq, id, data);
}

int lib_main( void** ctx, int (**command)(void*, int, void*) )
{
	bool prefer_ipv6 = false, run_daemon = false;
	const char *ua_eprm = NULL;
	const char *exec = NULL;
	const char *sw_version = "baresip v" BARESIP_VERSION " (" ARCH "/" OS ")";
	int err;

	(void)sys_coredump_set(true);

	err = libre_init();
	if(err) goto out;

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

	// Load modules
	err = conf_modules();
	if (err) goto out;

	// Launch as deamon
	if (run_daemon) {
		err = sys_daemon();
		if (err) goto out;

		log_enable_stderr(false);
	}

	info("baresip is ready.\n");

	if (exec)
		ui_input_str(exec);

	// Init Controls
	err = lib_init_control( ctx );
	if (err) goto out;

	*command = push;

	// Main Loop
	err = re_main(signal_handler);

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

