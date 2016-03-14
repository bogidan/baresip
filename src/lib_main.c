
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

static int conf_modules(void);

int lib_main()
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

	/*{
		size_t i;
		const char modv[] = {""};
		for( i = 0; i < lengthOf(modv); i++ ) { // Preload Modules if needed
			info("pre-loading modules\n");

			err = module_preload();
			if (err) {
				re_fprintf(stderr, "could not pre-load module '%s' (%m)\n", modv[i], err);
			}
		}
	} // */

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

	// Main loop
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

