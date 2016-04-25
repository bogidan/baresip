
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <Avrt.h>
#pragma comment(lib, "Avrt.lib")
void make_audio_thread()
{
	DWORD err, taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics("Pro Audio", &taskIndex);
	switch(err = (hTask==NULL) ? GetLastError() : NO_ERROR) {
	case NO_ERROR: break;
	default:
		info("sinwave: Failed to set 'Pro Audio' thread\n");
		//	case ERROR_INVALID_TASK_INDEX: BREAK;
		//	case ERROR_INVALID_TASK_NAME:  BREAK;
		//	case ERROR_PRIVILEGE_NOT_HELD: BREAK;
		//DEFAULT("Error setting MMCS thread characteristics [%d] %s\n", err, str_win32error(err) ) break;
	}

	switch(err = (AvSetMmThreadPriority( hTask, AVRT_PRIORITY_CRITICAL )) ? NO_ERROR : GetLastError()) {
	case NO_ERROR: break;
		info("sinwave: Failed to set AV Priority\n");
		//DEFAULT("Error setting MMCS priority [%d] %s\n", err, str_win32error(err) ) break;
	}
}
static uint64_t xtime()
{
	LARGE_INTEGER timestamp;
	QueryPerformanceCounter( &timestamp );
	return timestamp.QuadPart;
};
static uint64_t xtime_freq()
{
	LARGE_INTEGER timestamp;
	QueryPerformanceFrequency( &timestamp );
	return timestamp.QuadPart;
};
static uint64_t xtime_interval_ms(size_t ms)
{
	return xtime_freq() * ms / 1000;
}

// Implementation
//==============================================================================

struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	// Audio Thread
	HANDLE hThread;
	// Stream Information
	uint32_t srate, ptime; // sample_rate, frame time
	size_t sampc;          // sample count
	ausrc_read_h *rh;      // stream function
	bool  run;
	void *arg;
};

typedef void (*audio_ft)(int16_t *samples, size_t count);
void zeros(int16_t *samples, size_t count) {
	memset(samples, 0, count * sizeof(*samples));
}
audio_ft pull = zeros;
void lib_audio(audio_ft source, audio_ft sink) {
	pull = (source) ? source : zeros;
	(void) sink;
}

// Single audio thread for source and sink
static DWORD WINAPI audio_thread( void *arg )
{
	struct ausrc_st *st = arg;
	uint32_t idx = 0;
	int16_t *sampv;
	const uint64_t freq = xtime_freq();
	uint64_t now, ts;

	// Make Audio Thread
	make_audio_thread();

	// Initialize sample buffer
	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv) return 0;

	// push audio to softphone
	pull(sampv, st->sampc);
	st->rh(sampv, st->sampc, st->arg);

	// Initialize time keeping
	//  manually incremented for initial audio buffering
	ts   = xtime();

	// Loop thread to generate audio
	while (st->run) {

		now = xtime();
		if (ts > now) {
			Sleep(0);
			//sys_msleep(0); // TODO: better way of sleeping
			continue;
		}

		// fill buffer
		pull(sampv, st->sampc);
		
		// push audio to softphone
		st->rh(sampv, st->sampc, st->arg);

		// Compute samples played in processor frequency
		{
			static int64_t remainder = 0;
			// elapsed_ticks <= Hz * samples / sample_rate + carry
			const uint64_t raw = (freq * st->sampc) + remainder;
			uint64_t elapsed = raw / st->srate;

			// Compute remainder from division
			remainder        = raw % st->srate;

			// Adjust ticks for played audio.
			ts += elapsed;
		}
	}

	mem_deref(sampv);

	info("remote: player thread exited\n");

	return 0;
}

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	st->run = false;
	st->rh = NULL;
}

static int remote_src_alloc(
	struct ausrc_st **stp,
	const struct ausrc *as,
	struct media_ctx **ctx,
	struct ausrc_prm *prm,
	const char *device,
	ausrc_read_h *rh,
	ausrc_error_h *errh,
	void *arg
) {
	struct ausrc_st *st;
	int err;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;
	st->run = true;

	st->srate = prm->srate;
	st->ptime = prm->ptime;
	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	
	st->hThread = CreateThread(NULL, 4096, audio_thread, st, 0, NULL);
	err = (st->hThread == NULL);

	if (err) {
		mem_deref(st);
		st->rh = false;
	} else
		*stp = st;

	return err;
}


static struct ausrc *ausrc;
static struct auplay *auplay;

static int sw_init(void)
{
	int err;

	info("remote: Starting remote audio connection.\n");

	err  = ausrc_register (&ausrc,  "remote", remote_src_alloc);
	//err |= auplay_register(&auplay, "remote", remote_play_alloc);

	return err;
}

static int sw_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(remote) = {
	"remote",
	"sound",
	sw_init,
	sw_close
};


