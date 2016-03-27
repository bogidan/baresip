/**
 * @file module.c Module loading
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


struct modapp {
	struct mod *mod;
	struct le le;
};


static struct list modappl;


static void modapp_destructor(void *arg)
{
	struct modapp *modapp = arg;
	list_unlink(&modapp->le);
	mem_deref(modapp->mod);
}


#ifdef STATIC

/* Declared in static.c */
extern const struct mod_export *mod_table[];
extern const char* lib_modules[];
extern const char* lib_modules_temp[];
extern const char* lib_modules_app[];

static const struct mod_export *find_module(const struct pl *pl)
{
	struct pl name;
	uint32_t i;

	if (re_regex(pl->p, pl->l, "[^.]+.[^]*", &name, NULL))
		name = *pl;

	for (i=0; ; i++) {
		const struct mod_export *me = mod_table[i];
		if (!me)
			return NULL;
		if (0 == pl_strcasecmp(&name, me->name))
			return me;
	}

	return NULL;
}
#endif


static int load_module(struct mod **modp, const struct pl *modpath,
		       const struct pl *name)
{
	char file[256];
	struct mod *m = NULL;
	int err = 0;

	if (!name)
		return EINVAL;

#ifdef STATIC
	/* Try static first */
	err = mod_add(&m, find_module(name));
	if (!err)
		goto out;
#endif

	/* Then dynamic */
	if (re_snprintf(file, sizeof(file), "%r/%r", modpath, name) < 0) {
		err = ENOMEM;
		goto out;
	}
	err = mod_load(&m, file);
	if (err)
		goto out;

 out:
	if (err) {
		warning("module %r: %m\n", name, err);
	}
	else if (modp)
		*modp = m;

	return err;
}


static int module_handler(const struct pl *val, void *arg)
{
	(void)load_module(NULL, arg, val);
	return 0;
}


static int module_tmp_handler(const struct pl *val, void *arg)
{
	struct mod *mod = NULL;
	(void)load_module(&mod, arg, val);
	mem_deref(mod);
	return 0;
}


static int module_app_handler(const struct pl *val, void *arg)
{
	struct modapp *modapp;

	modapp = mem_zalloc(sizeof(*modapp), modapp_destructor);
	if (!modapp)
		return ENOMEM;

	if (load_module(&modapp->mod, arg, val)) {
		mem_deref(modapp);
		return 0;
	}

	list_prepend(&modappl, &modapp->le, modapp);

	return 0;
}


int module_init(const struct conf *conf)
{
	struct pl path;
	int err;

	if (!conf)
		return EINVAL;

	if (conf_get(conf, "module_path", &path))
		pl_set_str(&path, ".");

	err = conf_apply(conf, "module", module_handler, &path);
	if (err)
		return err;

	err = conf_apply(conf, "module_tmp", module_tmp_handler, &path);
	if (err)
		return err;

	err = conf_apply(conf, "module_app", module_app_handler, &path);
	if (err)
		return err;

	return 0;
}
/*
# UI Modules
module			wincons.dll

# Audio codec Modules (in order)
#module			opus.dll
#module			silk.dll
#module			amr.dll
#module			g7221.dll
#module			g722.dll
#module			g726.dll
module			g711.dll
#module			gsm.dll
#module			l16.dll
#module			speex.dll
#module			bv32.dll

# Audio filter Modules (in encoding order)
module			vumeter.dll
module			vumeter.dll
#module			sndfile.dll
#module			speex_aec.dll
#module			speex_pp.dll
#module			plc.dll

# Audio driver Modules
#module			winwave.dll
module			sinwave.dll
module			testplayer.dll
#module			portaudio.dll

## Media NAT modules
module			stun.dll
module			turn.dll
module			ice.dll
#module			natpmp.dll

## Media encryption modules
#module			srtp.dll
#module			dtls_srtp.dll

## Temporary Modules (loaded then unloaded)
module_tmp		uuid.dll
module_tmp		account.dll
*/
#define lengthOf(a) (sizeof(a)/sizeof(*a))

static int lib_module_loader( const char* arr[], size_t len, int (*func)(const struct pl *val, void *arg), struct pl *path )
{
	struct pl val;
	size_t i;
	int err = 0;

	for( i = 0; i < len; i++ ) {
		pl_set_str(&val, arr[i]);
		err = func(&val, path);
		if( err ) break;
	}

	return err;
}

int lib_module_init()
{
	struct pl path;
	int err = 0;
	pl_set_str(&path, ".");

	err = lib_module_loader( lib_modules,      lengthOf(lib_modules),      module_handler,     &path);
	if( err ) return err;

	err = lib_module_loader( lib_modules_temp, lengthOf(lib_modules_temp), module_tmp_handler, &path);
	if( err ) return err;

	err = lib_module_loader( lib_modules_app,  lengthOf(lib_modules_app),  module_app_handler, &path);
	if( err ) return err;

	return err;
}


void module_app_unload(void)
{
	list_flush(&modappl);
}


int module_preload(const char *module)
{
	struct pl path, name;

	if (!module)
		return EINVAL;

	pl_set_str(&path, ".");
	pl_set_str(&name, module);

	return load_module(NULL, &path, &name);
}
