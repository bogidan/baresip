/**
 * @file sinwave/src.c Sine Wave Generator -- source
 *
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <baresip.h>
#include "sinwave.h"

#define READ_BUFFERS   4
#define INC_RPOS(a) ((a) = (((a) + 1) % READ_BUFFERS))

struct generator {
	float amplitude, Hz, sample_rate;
};

#define PI2 6.2831853071
#include <math.h>
int16_t generator( struct generator *gen, uint32_t idx) {
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
	struct generator gen;
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

static int add_wave_in(struct ausrc_st *st)
{
	struct dspbuf *db = &st->bufs[st->bufs_idx];
	WAVEHDR *wh = &db->wh;
	MMRESULT res;

	wh->lpData          = (LPSTR)db->mb->buf;
	wh->dwBufferLength  = db->mb->size;
	wh->dwBytesRecorded = 0;
	wh->dwFlags         = 0;
	wh->dwUser          = (DWORD_PTR)db->mb;

	waveInPrepareHeader(st->wavein, wh, sizeof(*wh));
	res = waveInAddBuffer(st->wavein, wh, sizeof(*wh));
	if (res != MMSYSERR_NOERROR) {
		warning("sinwave: add_wave_in: waveInAddBuffer fail: %08x\n",
			res);
		return ENOMEM;
	}

	INC_RPOS(st->bufs_idx);

	st->inuse++;

	return 0;
}

static void CALLBACK waveInCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	struct ausrc_st *st = (struct ausrc_st *)dwInstance;
	WAVEHDR *wh = (WAVEHDR *)dwParam1;
	static size_t gen_idx = 0;
	size_t i = 0;

	(void)hwo;
	(void)dwParam2;

	if (!st->rh)
		return;


	switch (uMsg) {
	default: break;
	case WIM_CLOSE: break;
	case WIM_OPEN:  break;
	case WIM_DATA:
		if (st->inuse < (READ_BUFFERS-1))
			add_wave_in(st);

		for( i = 0; i < wh->dwBytesRecorded/2; i++ ) {
			((int16_t*)wh->lpData)[i] = generator(&st->gen, gen_idx++);
		}

		st->rh((void *)wh->lpData, wh->dwBytesRecorded/2, st->arg);

		waveInUnprepareHeader(st->wavein, wh, sizeof(*wh));
		st->inuse--;
		break;
	}
}

static unsigned int find_dev(const char *name)
{
	WAVEINCAPS wic;
	unsigned int i, nInDevices = waveInGetNumDevs();

	if (!str_isset(name))
		return WAVE_MAPPER;

	for (i=0; i<nInDevices; i++) {
		if (waveInGetDevCaps(i, &wic,
				     sizeof(WAVEINCAPS))==MMSYSERR_NOERROR) {

			if (0 == str_casecmp(name, wic.szPname)) {
				return i;
			}
		}
	}

	return WAVE_MAPPER;
}

static int read_stream_open(struct ausrc_st *st, const struct ausrc_prm *prm,
			    unsigned int dev)
{
	WAVEFORMATEX wfmt;
	MMRESULT res;
	uint32_t sampc;
	int i, err = 0;

	/* Open an audio INPUT stream. */
	st->wavein = NULL;
	st->bufs_idx = 0;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;

	for (i = 0; i < READ_BUFFERS; i++) {
		memset(&st->bufs[i].wh, 0, sizeof(WAVEHDR));
		st->bufs[i].mb = mbuf_alloc(2 * sampc);
		if (!st->bufs[i].mb)
			return ENOMEM;
	}

	wfmt.wFormatTag      = WAVE_FORMAT_PCM;
	wfmt.nChannels       = prm->ch;
	wfmt.nSamplesPerSec  = prm->srate;
	wfmt.wBitsPerSample  = 16;
	wfmt.nBlockAlign     = (prm->ch * wfmt.wBitsPerSample) / 8;
	wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;
	wfmt.cbSize          = 0;

	res = waveInOpen(&st->wavein, dev, &wfmt,
			  (DWORD_PTR) waveInCallback,
			  (DWORD_PTR) st,
			  CALLBACK_FUNCTION | WAVE_FORMAT_DIRECT);
	if (res != MMSYSERR_NOERROR) {
		warning("sinwave: waveInOpen: failed %d\n", err);
		return EINVAL;
	}

	/* Prepare enough IN buffers to suite at least 50ms of data */
	for (i = 0; i < READ_BUFFERS; i++)
		err |= add_wave_in(st);

	waveInStart(st->wavein);

	return err;
}

void make_audio_thread();

static DWORD WINAPI sin_thread( void *arg )
{
	struct ausrc_st *st = arg;
	uint32_t idx = 0;
	int16_t *sampv;
	const uint64_t freq = xtime_freq();
	uint64_t now, ts;
	size_t i;

	// Make Audio Thread
	make_audio_thread();

	// Initialize sample buffer
	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv) return 0;

	// Generate sin wave audio
	for( i = 0; i < st->sampc; i++ ) sampv[i] = generator(&st->gen, idx++);
	// push audio to softphone
	st->rh(sampv, st->sampc, st->arg);

	// Initialize time keeping
	//  manually incremented for initial audio buffering
	ts   = xtime();

	// Loop thread to generate audio
	while (st->rh) {
		size_t i;

		now = xtime();
		if (ts > now) {
			Sleep(0);
			//sys_msleep(0); // TODO: better way of sleeping
			continue;
		}

		// Generate sin wave audio
		for( i = 0; i < st->sampc; i++ )
			sampv[i] = generator(&st->gen, idx++);

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

	info("sinwave: player thread exited\n");

	return 0;
}

int sinwave_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
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
	st->gen.sample_rate = (float) prm->srate; //(float)(prm->srate * prm->ch * prm->ptime / 1000);

	err = read_stream_open(st, prm, find_dev(device));
//	st->hThread = CreateThread(NULL, 4096, sin_thread, st, 0, NULL);
//	err = (st->hThread == NULL);

	if (err) {
		mem_deref(st);
		st->rh = false;
	} else
		*stp = st;

	return err;
}
