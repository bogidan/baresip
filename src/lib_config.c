
#include <string.h>
#include <dirent.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


#undef MOD_PRE
#define MOD_PRE ""  /**< Module prefix */


/** Core Run-time Configuration - populated from config file */
/** @todo: move config parsing/decoding to a module */
static struct config core_config = {

	/** SIP User-Agent */
	{
		128, // transaction bucket size // 16,
		"", // uuid
		"0.0.0.0:5060", // sip_listen
		"", // sip_certificate
	},

	/** Audio */
	{
		"sinwave","nil", // Source module, device
		"sinwave","nil", // Play module, device
		"sinwave","nil", // Alert module, device
		{8000, 48000}, // Saample rate range
		{1, 2}, // Channels
		0, 0, 0, 0, // Defaults
		false, // audio source opened first
		AUDIO_MODE_POLL,
	},

#ifdef USE_VIDEO
	/** Video */
	{
		"", "",
		"", "",
		352, 288,
		500000,
		25,
	},
#endif

	/** Audio/Video Transport */
	{
		0xb8,
		{1024, 49152},
		{0, 0},
		true,
		false,
		{5, 10},
		false
	},

	/* Network */
	{
		""
	},

#ifdef USE_VIDEO
	/* BFCP */
	{
		""
	},
#endif
};

int lib_config_parse_conf(struct config *cfg, const struct conf *conf)
{
	struct pl pollm, as, ap;
	enum poll_method method;
	struct vidsz size = {0, 0};
	uint32_t v;
	int err = 0;

	if (!cfg || !conf)
		return EINVAL;

	// Core & Poll Method
	method = METHOD_SELECT;
	err = poll_method_set(method);
	if (err) {
		warning("config: poll method error: %m\n", err);
	}

	return err;
}


static const char *default_audio_device(void)
{
#if defined (ANDROID)
	return "opensles";
#elif defined (DARWIN)
	return "coreaudio,nil";
#elif defined (FREEBSD)
	return "oss,/dev/dsp";
#elif defined (OPENBSD)
	return "sndio,default";
#elif defined (WIN32)
	return "winwave,nil";
#else
	return "alsa,default";
#endif
}


#ifdef USE_VIDEO
static const char *default_video_device(void)
{
#ifdef DARWIN

#ifdef QTCAPTURE_RUNLOOP
	return "qtcapture,nil";
#else
	return "avcapture,nil";
#endif

#else
	return "v4l2,/dev/video0";
#endif
}


static const char *default_video_display(void)
{
#ifdef DARWIN
	return "opengl,nil";
#else
	return "x11,nil";
#endif
}
#endif


static int default_interface_print(struct re_printf *pf, void *unused)
{
	char ifname[64];
	(void)unused;

	if (0 == net_rt_default_get(AF_INET, ifname, sizeof(ifname)))
		return re_hprintf(pf, "%s", ifname);
	else
		return re_hprintf(pf, "eth0");
}


static int core_config_template(struct re_printf *pf, const struct config *cfg)
{
	int err = 0;

	if (!cfg)
		return 0;

	err |= re_hprintf(pf,
			  "\n# Core\n"
			  "poll_method\t\t%s\t\t# poll, select"
#ifdef HAVE_EPOLL
				", epoll .."
#endif
#ifdef HAVE_KQUEUE
				", kqueue .."
#endif
				"\n"
			  "\n# SIP\n"
			  "sip_trans_bsize\t\t128\n"
			  "#sip_listen\t\t0.0.0.0:5060\n"
			  "#sip_certificate\tcert.pem\n"
			  "\n# Audio\n"
			  "audio_player\t\t%s\n"
			  "audio_source\t\t%s\n"
			  "audio_alert\t\t%s\n"
			  "audio_srate\t\t%u-%u\n"
			  "audio_channels\t\t%u-%u\n"
			  "#ausrc_srate\t\t48000\n"
			  "#auplay_srate\t\t48000\n"
			  "#ausrc_channels\t\t0\n"
			  "#auplay_channels\t\t0\n"
			  ,
			  poll_method_name(poll_method_best()),
			  default_audio_device(),
			  default_audio_device(),
			  default_audio_device(),
			  cfg->audio.srate.min, cfg->audio.srate.max,
			  cfg->audio.channels.min, cfg->audio.channels.max);

#ifdef USE_VIDEO
	err |= re_hprintf(pf,
			  "\n# Video\n"
			  "#video_source\t\t%s\n"
			  "#video_display\t\t%s\n"
			  "video_size\t\t%dx%d\n"
			  "video_bitrate\t\t%u\n"
			  "video_fps\t\t%u\n",
			  default_video_device(),
			  default_video_display(),
			  cfg->video.width, cfg->video.height,
			  cfg->video.bitrate, cfg->video.fps);
#endif

	err |= re_hprintf(pf,
			  "\n# AVT - Audio/Video Transport\n"
			  "rtp_tos\t\t\t184\n"
			  "#rtp_ports\t\t10000-20000\n"
			  "#rtp_bandwidth\t\t512-1024 # [kbit/s]\n"
			  "rtcp_enable\t\tyes\n"
			  "rtcp_mux\t\tno\n"
			  "jitter_buffer_delay\t%u-%u\t\t# frames\n"
			  "rtp_stats\t\tno\n"
			  "\n# Network\n"
			  "#dns_server\t\t10.0.0.1:53\n"
			  "#net_interface\t\t%H\n",
			  cfg->avt.jbuf_del.min, cfg->avt.jbuf_del.max,
			  default_interface_print, NULL);

#ifdef USE_VIDEO
	err |= re_hprintf(pf,
			  "\n# BFCP\n"
			  "#bfcp_proto\t\tudp\n");
#endif

	return err;
}


static uint32_t count_modules(const char *path)
{
	DIR *dirp;
	struct dirent *dp;
	uint32_t n = 0;

	dirp = opendir(path);
	if (!dirp)
		return 0;

	while ((dp = readdir(dirp)) != NULL) {

		size_t len = strlen(dp->d_name);
		const size_t x = sizeof(MOD_EXT)-1;

		if (len <= x)
			continue;

		if (0==memcmp(&dp->d_name[len-x], MOD_EXT, x))
			++n;
	}

	(void)closedir(dirp);

	return n;
}


static const char *detect_module_path(bool *valid)
{
	static const char * const pathv[] = {
#if defined (PREFIX)
		"" PREFIX "/lib/baresip/modules",
#else
		"/usr/local/lib/baresip/modules",
		"/usr/lib/baresip/modules",
#endif
	};
	const char *current = pathv[0];
	uint32_t nmax = 0;
	size_t i;

	for (i=0; i<ARRAY_SIZE(pathv); i++) {

		uint32_t n = count_modules(pathv[i]);

		info("%s: detected %u modules\n", pathv[i], n);

		if (n > nmax) {
			nmax = n;
			current = pathv[i];
		}
	}

	if (nmax > 0)
		*valid = true;

	return current;
}


int config_write_template(const char *file, const struct config *cfg)
{
	FILE *f = NULL;
	int err = 0;
	const char *modpath;
	bool modpath_valid = false;

	if (!file || !cfg)
		return EINVAL;

	info("config: creating config template %s\n", file);

	f = fopen(file, "w");
	if (!f) {
		warning("config: writing %s: %m\n", file, errno);
		return errno;
	}

	(void)re_fprintf(f,
			 "#\n"
			 "# baresip configuration\n"
			 "#\n"
			 "\n"
			 "#------------------------------------"
			 "------------------------------------------\n");

	(void)re_fprintf(f, "%H", core_config_template, cfg);

	(void)re_fprintf(f,
			 "\n#------------------------------------"
			 "------------------------------------------\n"
			 "# Modules\n"
			 "\n");

	modpath = detect_module_path(&modpath_valid);
	(void)re_fprintf(f, "%smodule_path\t\t%s\n",
			 modpath_valid ? "" : "#", modpath);

	(void)re_fprintf(f, "\n# UI Modules\n");
#if defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "wincons" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "stdio" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "cons" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "evdev" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "httpd" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio codec Modules (in order)\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "opus" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "silk" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "amr" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g7221" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g722" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "g726" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "g711" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "gsm" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "l16" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "bv32" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "vumeter" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "sndfile" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex_aec" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "speex_pp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "plc" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Audio driver Modules\n");
#if defined (ANDROID)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "opensles" MOD_EXT "\n");
#elif defined (DARWIN)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "coreaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "audiounit" MOD_EXT "\n");
#elif defined (FREEBSD)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "oss" MOD_EXT "\n");
#elif defined (OPENBSD)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "sndio" MOD_EXT "\n");
#elif defined (WIN32)
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "winwave" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "alsa" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "portaudio" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "aubridge" MOD_EXT "\n");

#ifdef USE_VIDEO

	(void)re_fprintf(f, "\n# Video codec Modules (in order)\n");
#ifdef USE_AVCODEC
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "avcodec" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "avcodec" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "vpx" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video filter Modules (in encoding order)\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "selfview" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video source modules\n");
#if defined (DARWIN)

#ifdef QTCAPTURE_RUNLOOP
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "qtcapture" MOD_EXT "\n");
#else
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "avcapture" MOD_EXT "\n");
#endif

#else
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "v4l" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "v4l2" MOD_EXT "\n");
#endif
#ifdef USE_AVFORMAT
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "avformat" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "x11grab" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "cairo" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Video display modules\n");
#ifdef DARWIN
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "opengl" MOD_EXT "\n");
#endif
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "x11" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "sdl2" MOD_EXT "\n");

#endif /* USE_VIDEO */

	(void)re_fprintf(f,
			"\n# Audio/Video source modules\n"
			"#module\t\t\t" MOD_PRE "rst" MOD_EXT "\n"
			"#module\t\t\t" MOD_PRE "gst" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media NAT modules\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "stun" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "turn" MOD_EXT "\n");
	(void)re_fprintf(f, "module\t\t\t" MOD_PRE "ice" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "natpmp" MOD_EXT "\n");

	(void)re_fprintf(f, "\n# Media encryption modules\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "#module\t\t\t" MOD_PRE "dtls_srtp" MOD_EXT "\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Temporary Modules (loaded then unloaded)\n");
	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "module_tmp\t\t" MOD_PRE "uuid" MOD_EXT "\n");
	(void)re_fprintf(f, "module_tmp\t\t" MOD_PRE "account" MOD_EXT "\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Application Modules\n");
	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "module_app\t\t" MOD_PRE "auloop"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  MOD_PRE "contact"MOD_EXT"\n");
	(void)re_fprintf(f, "module_app\t\t"  MOD_PRE "menu"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t"  MOD_PRE "mwi"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "natbd"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "presence"MOD_EXT"\n");
	(void)re_fprintf(f, "#module_app\t\t" MOD_PRE "syslog"MOD_EXT"\n");
#ifdef USE_VIDEO
	(void)re_fprintf(f, "module_app\t\t" MOD_PRE "vidloop"MOD_EXT"\n");
#endif
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n#------------------------------------"
			 "------------------------------------------\n");
	(void)re_fprintf(f, "# Module parameters\n");
	(void)re_fprintf(f, "\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "cons_listen\t\t0.0.0.0:5555\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "http_listen\t\t0.0.0.0:8000\n");

	(void)re_fprintf(f, "\n");
	(void)re_fprintf(f, "evdev_device\t\t/dev/input/event0\n");

	(void)re_fprintf(f, "\n# Speex codec parameters\n");
	(void)re_fprintf(f, "speex_quality\t\t7 # 0-10\n");
	(void)re_fprintf(f, "speex_complexity\t7 # 0-10\n");
	(void)re_fprintf(f, "speex_enhancement\t0 # 0-1\n");
	(void)re_fprintf(f, "speex_mode_nb\t\t3 # 1-6\n");
	(void)re_fprintf(f, "speex_mode_wb\t\t6 # 1-6\n");
	(void)re_fprintf(f, "speex_vbr\t\t0 # Variable Bit Rate 0-1\n");
	(void)re_fprintf(f, "speex_vad\t\t0 # Voice Activity Detection 0-1\n");
	(void)re_fprintf(f, "speex_agc_level\t\t8000\n");

	(void)re_fprintf(f, "\n# Opus codec parameters\n");
	(void)re_fprintf(f, "opus_bitrate\t\t28000 # 6000-510000\n");

	(void)re_fprintf(f, "\n# NAT Behavior Discovery\n");
	(void)re_fprintf(f, "natbd_server\t\tcreytiv.com\n");
	(void)re_fprintf(f, "natbd_interval\t\t600\t\t# in seconds\n");

	(void)re_fprintf(f,
			"\n# Selfview\n"
			"video_selfview\t\twindow # {window,pip}\n"
			"#selfview_size\t\t64x64\n");

	(void)re_fprintf(f,
			"\n# ICE\n"
			"ice_turn\t\tno\n"
			"ice_debug\t\tno\n"
			"ice_nomination\t\tregular\t# {regular,aggressive}\n"
			"ice_mode\t\tfull\t# {full,lite}\n");

	if (f)
		(void)fclose(f);

	return err;
}


struct config *conf_config(void)
{
	return &core_config;
}
