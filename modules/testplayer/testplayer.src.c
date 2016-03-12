
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <baresip.h>
#include "testplayer.h"

#define READ_BUFFERS   4
#define INC_RPOS(a) ((a) = (((a) + 1) % READ_BUFFERS))

struct generator2 {
	float amplitude, Hz, sample_rate;
};

#define PI2 6.2831853071
#include <math.h>
static int16_t generator( struct generator2 *gen, uint32_t idx) {
	// (sin (* 2 pi (/ idx 22050) 440))
	float v = gen->amplitude * (float) sin( PI2 * (idx / gen->sample_rate) * gen->Hz);
	return (int16_t)(v * ((1<<15) - 1));
};

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


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	// Stream Information
	uint32_t srate, ptime; // sample_rate, frame time
	size_t sampc;          // sample count
	ausrc_read_h *rh;      // stream function
	// WinWave Recorder
	HWAVEIN wavein;
	size_t bufs_idx;
	struct dspbuf bufs[READ_BUFFERS];
	size_t inuse;
	void *arg;
	// Audio Generator
	struct generator2 gen;
	HANDLE hThread;
	// Audio File Player
//	struct tmr tmr;
//	bool run;
};

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	int i;

	st->rh = NULL;

	waveInStop(st->wavein);
	waveInReset(st->wavein);

	Sleep(4);

	for (i = 0; i < READ_BUFFERS; i++) {
		waveInUnprepareHeader(st->wavein, &st->bufs[i].wh,
				      sizeof(WAVEHDR));
		mem_deref(st->bufs[i].mb);
	}

	waveInClose(st->wavein);
}

#include <Avrt.h>
#pragma comment(lib, "Avrt.lib")
static void make_audio_thread()
{
	DWORD err, taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics("Pro Audio", &taskIndex);
	switch(err = (hTask==NULL) ? GetLastError() : NO_ERROR) {
	case NO_ERROR: break;
	default:
		info("testplayer: Failed to set 'Pro Audio' thread\n");
		//	case ERROR_INVALID_TASK_INDEX: BREAK;
		//	case ERROR_INVALID_TASK_NAME:  BREAK;
		//	case ERROR_PRIVILEGE_NOT_HELD: BREAK;
		//DEFAULT("Error setting MMCS thread characteristics [%d] %s\n", err, str_win32error(err) ) break;
	}

	switch(err = (AvSetMmThreadPriority( hTask, AVRT_PRIORITY_CRITICAL )) ? NO_ERROR : GetLastError()) {
	case NO_ERROR: break;
		info("testplayer: Failed to set AV Priority\n");
		//DEFAULT("Error setting MMCS priority [%d] %s\n", err, str_win32error(err) ) break;
	}
}

#include "player.c" 
static DWORD WINAPI audio_thread( void *arg )
{
	struct ausrc_st *st = arg;
	uint32_t idx = 0;
	int16_t *sampv;
	const uint64_t freq = xtime_freq();
	uint64_t now, ts;
	audio_file_t audio;

	// Make Audio Thread
	make_audio_thread();
	open_audio("background.wav", &audio);

	// Initialize sample buffer
	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv) return 0;

	play_audio(&audio, sampv, st->sampc, st->srate);
	st->rh(sampv, st->sampc, st->arg);

	// Initialize time keeping
	//  manually incremented for initial audio buffering
	ts   = xtime();

	// Loop thread to generate audio
	while (st->rh) {
		now = xtime();
		if (ts > now) {
			Sleep(0);
			//sys_msleep(0); // TODO: better way of sleeping
			continue;
		}

		// push audio to softphone
		play_audio(&audio, sampv, st->sampc, st->srate);
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

	info("testplayer: player thread exited\n");

	return 0;
}

int testplayer_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
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

	st->srate = prm->srate;
	st->ptime = prm->ptime;
	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	
	// Initialize Wave Generator
	st->gen.amplitude   = 0.2f;
	st->gen.Hz          = 440.0f;
	st->gen.sample_rate = (float)prm->srate; //(float)(prm->srate * prm->ch * prm->ptime / 1000);

//	err = read_stream_open(st, prm, find_dev(device));
	st->hThread = CreateThread(NULL, 4096, audio_thread, st, 0, NULL);
	err = (st->hThread == NULL);

	if (err) {
		mem_deref(st);
		st->rh = false;
	} else
		*stp = st;

	return err;
}
