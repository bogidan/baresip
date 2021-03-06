
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "testplayer.h"


/**
 * @defgroup winwave winwave
 *
 * Sine Audio Generation module.
 *
 */

static struct ausrc *ausrc;
static struct auplay *auplay;


static int sw_init(void)
{
	int play_dev_count, src_dev_count;
	int err;

	play_dev_count = waveOutGetNumDevs();
	src_dev_count = waveInGetNumDevs();

	info("testplayer: output devices: %d, input devices: %d\n",
	     play_dev_count, src_dev_count);

	err  = ausrc_register(&ausrc, "testplayer", testplayer_src_alloc);
	err |= auplay_register(&auplay, "testplayer", testplayer_play_alloc);

	return err;
}


static int sw_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(testplayer) = {
	"testplayer",
	"sound",
	sw_init,
	sw_close
};
