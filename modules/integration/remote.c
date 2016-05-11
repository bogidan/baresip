
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

typedef void (*audio_ft)(int16_t *samples, size_t count);
static void zeros(int16_t *samples, size_t count) {
	memset(samples, 0, count * sizeof(*samples));
}
static void null_sink(int16_t *samples, size_t count) {
	(void) samples;
	(void) count;
}
audio_ft pull = zeros, push = null_sink;
void lib_audio(audio_ft source, audio_ft sink) {
	pull = (source) ? source : zeros;
	push = (sink)   ? sink   : null_sink;
}

// Only one thread should be created per connection.
struct authread_st {
	size_t cref;    // Ref count
	HANDLE hThread; // Audio Thread
	bool   run;
	// Stream Information
	uint32_t srate, ptime; // sample_rate, frame time
	size_t sampc;          // sample count
	// Stream Reader
	ausrc_read_h   *rh;
	void           *rarg;
	// Stream Writer
	auplay_write_h *wh;
	void           *warg;
};
struct ausrc_st {
	const struct ausrc *as; // inheritance
	struct authread_st *th;
};
struct auplay_st {
	const struct auplay *ap; // inheritance
	struct authread_st *th;
};

static struct authread_st authread = {0};

// Single audio thread for source and sink
static DWORD WINAPI authread_proc( void *arg )
{
	struct authread_st *th = arg;
	int16_t *sampv;
	const uint64_t freq = xtime_freq();
	uint64_t now, ts;

	// Make Audio Thread
	make_audio_thread();

	// Initialize sample buffer
	sampv = mem_alloc(th->sampc * sizeof(int16_t), NULL);
	if (!sampv) return 0;

	// (prepush) audio to softphone
	//pull(sampv, th->sampc);
	//th->rh(sampv, th->sampc, th->rarg);

	// Initialize time keeping
	//  manually incremented for initial audio buffering
	ts   = xtime();

	// Loop thread to generate audio
	while (th->run) {

		now = xtime();
		if (ts > now) {
			Sleep(0);
			//sys_msleep(0); // TODO: better way of sleeping
			continue;
		}

		// push audio to softphone
		pull(sampv, th->sampc);
		if(th->rh) th->rh(sampv, th->sampc, th->rarg);
		// pull audio from softphone
		if(th->wh) th->wh(sampv, th->sampc, th->warg);
		push(sampv, th->sampc);

		// Compute samples played in processor frequency
		{
			static int64_t remainder = 0;
			// elapsed_ticks <= Hz * samples / sample_rate + carry
			const uint64_t raw = (freq * th->sampc) + remainder;
			uint64_t elapsed = raw / th->srate;

			// Compute remainder from division
			remainder        = raw % th->srate;

			// Adjust ticks for played audio.
			ts += elapsed;
		}
	}

	mem_deref(sampv);

	info("remote: player thread exited\n");

	return 0;
}
static int create_authread( struct authread_st *th, uint32_t srate, uint32_t ptime, size_t sampc )
{

	int err = 0;
	th->run = true;
	if(th->cref++ == 0) {
		th->hThread = CreateThread(NULL, 4096, authread_proc, th, 0, NULL);

		if( th->hThread == NULL ) {
			err = -1;
		}

		th->srate = srate;
		th->ptime = ptime;
		th->sampc = sampc;
	} else { // Verify Stream information
		if( th->srate != srate || th->ptime != ptime || th->sampc != sampc ) {
			error("Second authread parameters do not match first");
			err = true;
		}
	}
	return err;
}
static void destroy_authread( struct authread_st *th )
{
	if(--th->cref == 0) {
		th->run = false;
	}
}

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	destroy_authread(st->th);
	st->th->rh   = 0;
	st->th->rarg = 0;
}
static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	destroy_authread(st->th);
	st->th->wh   = 0;
	st->th->warg = 0;
}

static int remote_src_alloc(
	struct ausrc_st **stp, const struct ausrc *as,
	struct media_ctx **ctx, struct ausrc_prm *prm,
	const char *device, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg
) {
	struct ausrc_st *st;
	struct authread_st *th = &authread;
	int err;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as   = as;
	st->th   = th;
	th->rh   = rh;
	th->rarg = arg;

	err = create_authread(st->th, prm->srate, prm->ptime, prm->srate * prm->ch * prm->ptime / 1000);
	
	if (err) {
		mem_deref(st);
		th->rh = false;
	} else
		*stp = st;

	return err;
}

int remote_play_alloc(
	struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *device,
	auplay_write_h *wh, void *arg
) {
	struct auplay_st *st;
	struct authread_st *th = &authread;
	int err = 0;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->th   = th;
	th->wh   = wh;
	th->warg = arg;

	err = create_authread(st->th, prm->srate, prm->ptime, prm->srate * prm->ch * prm->ptime / 1000);

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}

static struct ausrc       *ausrc;
static struct auplay      *auplay;

static int sw_init(void)
{
	int err;

	info("remote: Starting remote audio connection.\n");

	err  = ausrc_register (&ausrc,  "remote", remote_src_alloc);
	err |= auplay_register(&auplay, "remote", remote_play_alloc);

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


