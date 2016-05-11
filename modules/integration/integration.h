#pragma once

typedef void (*audio_ft)(int16_t *samples, size_t count);
typedef void (*monitor_ft)(void *arg, enum barsip_et msg, void *data);

#ifdef __cplusplus
extern "C" {
#endif
int lib_main( struct baresip_control *control );
int lib_audio( audio_ft pull, audio_ft push );
int lib_control(void* ctx, enum baresip_et id, void* data);
#ifdef __cplusplus
}
#endif

enum baresip_et {
	// Control
	eQuit, eRegister, eAnswer, eHangup, eDial,
	// Status
	esInitialized, esStopped,
	esDTMF,
};


// Reference and function to push to the internal baresip queue
struct baresip_control {
	// Control Baresip
	void *ctx;
	// Monitor Barsip
	monitor_ft cb;
	void *arg;
};

// Push command onto the baresip internal queue
int baresip_push(struct baresip_control *control, enum baresip_et id, void *data) {
	if(control->ctx == NULL) return -1;
	return lib_control(control->ctx, id, data);
};

// Register sip account
int baresip_register(struct baresip_control *control, const char* account)
{
	return baresip_push(control, eRegister, (void*)account);
}

int baresip_answer(struct baresip_control *control)
{
	return baresip_push(control, eAnswer, NULL);
}

int baresip_hangup(struct baresip_control *control)
{
	return baresip_push(control, eHangup, NULL);
}

int baresip_dial(struct baresip_control *control, const char* uri)
{
	return baresip_push(control, eDial, (void*)uri);
}

int baresip_quit(struct baresip_control *control)
{
	return baresip_push(control, eQuit, NULL);
}
