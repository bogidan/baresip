#pragma once

typedef void (*audio_ft)(int16_t *samples, size_t count);

#ifdef __cplusplus
extern "C" {
#endif
int lib_main( struct baresip_control *control );
int lib_audio( audio_ft pull, audio_ft push );
#ifdef __cplusplus
}
#endif

enum baresip_control_et {
	eQuit, eRegister, eAnswer, eHangup, eDial,
};

// Reference and function to push to the internal baresip queue
struct baresip_control {
	void *ctx;
	int  (*cmd)(void *ctx, int id, void *data);
};

// Push command onto th baresip internal queue
int baresip_push(struct baresip_control *control, int id, void *data) {
	if(control->cmd == NULL) return -1;
	return control->cmd(control->ctx, id, data);
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
