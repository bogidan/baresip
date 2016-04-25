// lib_config.c - Handles configuration when linking baresip as a library.

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
	{	// SIP User-Agent
		128, // transaction bucket size // 16,
		"", // uuid
		"0.0.0.0:5060", // sip_listen
		"", // sip_certificate
	},
	{	// Audio
		"remote","nil", // Source module, device
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
	{	// Audio/Video Transport
		0xb8,
		{1024, 49152},
		{0, 0},
		true,
		false,
		{5, 10},
		false
	},
	{	// Network
		""
	},
#ifdef USE_VIDEO
	/* BFCP */
	{
		""
	},
#endif
};

int lib_config_parse_conf()
{
	enum poll_method method;
	int err = 0;

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

int lib_config_write_template(const char *file, const struct config *cfg)
{
	return 0;
}

struct config *lib_conf_config(void)
{
	return &core_config;
}
