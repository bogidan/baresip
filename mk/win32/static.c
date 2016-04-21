/* static.c - manually updated
	Holds files for static linking of modules.
*/
#include <re_types.h>
#include <re_mod.h>

extern const struct mod_export exports_cons;
extern const struct mod_export exports_wincons;
extern const struct mod_export exports_g711;
extern const struct mod_export exports_winwave;
extern const struct mod_export exports_dshow;
extern const struct mod_export exports_avcodec;
extern const struct mod_export exports_account;
extern const struct mod_export exports_contact;
extern const struct mod_export exports_menu;
extern const struct mod_export exports_auloop;
#ifdef USE_VIDEO
extern const struct mod_export exports_vidloop;
#endif
extern const struct mod_export exports_uuid;
extern const struct mod_export exports_stun;
extern const struct mod_export exports_turn;
extern const struct mod_export exports_ice;
extern const struct mod_export exports_vumeter;
// Custom Modules
extern const struct mod_export exports_sinwave;
extern const struct mod_export exports_testplayer;
//extern const struct mod_export exports_integration;
extern const struct mod_export exports_remote;


const struct mod_export *mod_table[] = {
	&exports_wincons,
	&exports_avcodec,
	&exports_g711,
	&exports_winwave,
	&exports_dshow,
	&exports_account,
	&exports_contact,
	&exports_menu,
	&exports_auloop,
#ifdef USE_VIDEO
	&exports_vidloop,
#endif
	&exports_uuid,
	&exports_stun,
	&exports_turn,
	&exports_ice,
	&exports_vumeter,
	// Custom
	&exports_sinwave,
	&exports_testplayer,
//	&exports_integration,
	&exports_remote,
	// Null Marking the end of the list.
	NULL
};


/* Module Extra Parameters

cons_listen		0.0.0.0:5555

http_listen		0.0.0.0:8000

evdev_device		/dev/input/event0

# Speex codec parameters
speex_quality		7 # 0-10
speex_complexity	7 # 0-10
speex_enhancement	0 # 0-1
speex_mode_nb		3 # 1-6
speex_mode_wb		6 # 1-6
speex_vbr		0 # Variable Bit Rate 0-1
speex_vad		0 # Voice Activity Detection 0-1
speex_agc_level		8000

# Opus codec parameters
opus_bitrate		28000 # 6000-510000

# NAT Behavior Discovery
natbd_server		creytiv.com
natbd_interval		600		# in seconds

# Selfview
video_selfview		window # {window,pip}
#selfview_size		64x64

# ICE
ice_turn		no
ice_debug		no
ice_nomination		regular	# {regular,aggressive}
ice_mode		full	# {full,lite}

*/
