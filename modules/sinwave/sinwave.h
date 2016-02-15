/**
 * @file sinewav.h generate am audio sine wave -- internal api
 *
 */


struct dspbuf {
	WAVEHDR      wh;
	struct mbuf *mb;
};


int sinwave_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int sinwave_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg);
