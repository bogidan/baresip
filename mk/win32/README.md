
# Conversion into source/sink audio interface

# Adding a new module

On adding a new module compilation of the file will fail because the temp directory for that module is not found. Running the build once generates the required folder.



# Conversion to vs2010 from BareSIP Git Repo

* Added common properties to all re-win32, rem-win32, baresip-win32.
* dlls needed by baresip:
	- avcodec-56.dll
	- avutil-54.dll
	- swresample-1.dll

## libre

* Added preprocessor definitions globally
	* WIN32_LEAN_AND_MEAN
	* HAVE_INET_PTON
	* HAVE_INET_NTOP
* Resolved bad casts
	* sip/msg.c
	* sipreg/reg.c
	* sipsess/listen.c
	* sipevent/listen.c
	* stun/keepalive.c
	* sdp/attr.c
* Resolved multiple sources mapping to the same .obj (a la Microsoft)
	* sdp/attr.c -> sdp/attr.sdp.c
	* sdp/msg.c -> sdp/msg.sdp.c
	* sip/msg.c -> sip/msg.sip.c
	* sip/reply.c -> sip/reply.sip.c
	* sip/request.c -> sip/request.sip.c
	* sip/keepalive.c -> sip/keepalive.sip.c
	* sipevent/listen.c -> sipevent/listen.sipevent.c
	* sipevent/msg.c -> sipevent/msg.sipevent.c
	* sipsess/reply.c -> sipsess/reply.sipsess.c
	* sipsess/request.c -> sipsess/request.sipsess.c
* Added existing but not included files
	* msg/ctype.c
	* msg/param.c (replaces missing sip/param.c)
	* mqueue/mqueue.c
	* mqueue/win32/pipe.c
	* sip/contact.c

## librem

No Changes

## baresip

* Added FFmpeg development library
	* ffmpeg path "..\..\..\ffmpeg-win32-dev"
* Added external 'inttypes.h' implementation because microsoft
	* [msinttypes](https://github.com/chemeris/msinttypes)
	* [original](https://code.google.com/p/msinttypes/)
* Added missing qedit.h from Windows SDK 7.0
* Added missing 'dirent.h', 'dirent.c' from POSIX also missing from Windows
	* [Directory browsing API for Windows](http://www.two-sdg.demon.co.uk/curbralan/code/dirent/dirent.html)
* Linked 'ws2_32.lib' library

### Removal of video from baresip

* Removed Video From Build && disabled USE_VIDEO preprocessor flag
	* dshow.c
	* video.c
	* vidfilt.c
	* vidisp.c
	* vidloop.c
	* vidsrc.c
* Tweaked 'static.c' to remove dshow and video
