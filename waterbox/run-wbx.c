/* run-wbx.c - drives core.wbx through the miniBox host over the same work
 * dir and schedule as run-native, reporting the same digests, so the two
 * builds diff directly. Every regular file in the work dir is mounted into
 * the guest under its basename - exactly what the frontend does with a
 * project's files, slot map and settings.
 *
 * usage: run-wbx <core.wbx> <workdir> [run-native's options] [--rerecord] [--turbo]
 *
 * --rerecord round-trips the WHOLE guest machine through the host's
 * save/load state around every frame; the digests must be identical.
 */
#include "minibox.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gate-harness.h"

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s) { return (intptr_t)fread(d, 1, s, ((freader *)ud)->f); }
typedef struct { uint8_t *b; size_t len, cap, pos; } membuf;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	if (m->len + n > m->cap) { m->cap = (m->len + n) * 2 + 64; m->b = realloc(m->b, m->cap); }
	memcpy(m->b + m->len, d, n); m->len += n; return 0;
}
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	uintptr_t avail = m->len - m->pos; if (n > avail) n = avail;
	memcpy(d, m->b + m->pos, n); m->pos += n; return (intptr_t)n;
}

typedef int (MB_GUEST_ABI *intfn)(void);
typedef void (MB_GUEST_ABI *framefn)(uint64_t);
typedef void (MB_GUEST_ABI *setfn)(int32_t, int32_t);
typedef void (MB_GUEST_ABI *voidfn_i)(int);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i)(int);
typedef int (MB_GUEST_ABI *intfn_i)(int);
typedef int64_t (MB_GUEST_ABI *i64fn_i)(int);
typedef int32_t (MB_GUEST_ABI *i32fn)(void);
typedef int32_t (MB_GUEST_ABI *i32fn_i)(int32_t);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i32)(int32_t);
typedef int64_t (MB_GUEST_ABI *i64fn_i32)(int32_t);

static mb_host *g_host;
#ifdef CHIMERA_GL_BRIDGE
int chimera_gl_host_init(char *err, int errlen);
const char *chimera_gl_host_description(void);
uintptr_t chimera_gl_host_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
void chimera_gl_host_state_loaded(void);
unsigned long chimera_gl_host_unhandled(long *last_op);
typedef void (*setfn_v)(uint64_t);

/* What chimera's session does after a load (ce_gl_state_loaded), said here so
 * this runner asks the renderer the same question the frontend does. Without
 * it a load in this runner is a strictly EASIER test than a load in Chimera,
 * and the gate would be standing behind the easier one. */
static void gl_state_loaded(void)
{
	chimera_gl_host_state_loaded();
}

/* Every call the bridge shrugged at. Zero is a plausible answer to nearly
 * every opcode, so a run nobody answered looks exactly like a run that was
 * answered - which is how GL_OP_CONTEXT_ID went unanswered here for as long as
 * the opcode existed. Said out loud so the gate can fail on it instead of the
 * log saying it to nobody. */
static void gl_report_unhandled(void)
{
	long last = 0;
	const unsigned long n = chimera_gl_host_unhandled(&last);
	if (n == 0) return;
	fprintf(stderr, "gpu bridge: %lu call(s) to opcodes this host has no case for"
		" (last: opcode %ld); every one was answered 0\n", n, last);
	fflush(stderr);
}
#else
static void gl_state_loaded(void) { }
static void gl_report_unhandled(void) { }
#endif

static intfn g_Init;
static ptrfn g_GetLoadError;
static setfn g_SetButton;
static setfn g_SetAxis;
static framefn g_FrameAdvance;
static ptrfn g_GetVideoBgra;
static intfn g_GetVideoWidth, g_GetVideoHeight;
static ptrfn g_GetAudio;
static intfn g_GetAudioSampleCount;
static intfn g_InputWasRead;
static intfn g_GetMemoryDomainCount;
static ptrfn_i g_GetMemoryDomainName, g_GetMemoryDomainPtr;
static i64fn_i g_GetMemoryDomainSize;
static intfn g_GetVsyncNumerator, g_GetVsyncDenominator;
static i32fn g_GetSaveDataFileCount;
static ptrfn_i32 g_GetSaveDataFileName, g_GetSaveDataFileBuffer;
static i64fn_i32 g_GetSaveDataFileSize;
static voidfn_i g_SetRenderingEnabled;
typedef void (MB_GUEST_ABI *voidfn)(void);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);
/* optional exports: absent in a core that has nothing to say (see proc_optional) */
static voidfn g_StateSaving, g_StateLoaded;
static u64fn g_ChimeraMachineDigest;
static int g_rerecord;
static membuf g_state;

static uintptr_t proc(mb_host *h, const char *n)
{
	mb_return r;
	wbx_get_proc_addr(h, n, &r);
	if (r.error_message[0]) { fprintf(stderr, "proc %s: %s\n", n, r.error_message); exit(2); }
	if (!r.data) { fprintf(stderr, "missing required export %s\n", n); exit(2); }
	return r.data;
}

/* An export a core may leave out. Three answers, not two: there, not there,
 * and could not be asked - which stops the run, because "could not be asked"
 * read as "not there" is a feature silently switched off. */
static uintptr_t proc_optional(mb_host *h, const char *n)
{
	mb_return r;
	wbx_get_proc_addr(h, n, &r);
	if (r.error_message[0]) { fprintf(stderr, "proc %s: %s\n", n, r.error_message); exit(2); }
	return (uintptr_t)r.data;
}

static int core_init(void) { return g_Init(); }
static int core_init_done(void) { return 1; } /* boot happens before Seal, not in gate_run */
static const char *core_load_error(void) { return (const char *)g_GetLoadError(); }
static void core_set_button(int32_t i, int32_t s) { g_SetButton(i, s); }
static void core_set_axis(int32_t i, int32_t v) { g_SetAxis(i, v); }
static void core_frame(void) { g_FrameAdvance(0); }
static void core_set_rendering(int on) { g_SetRenderingEnabled(on); }
static const uint32_t *core_video(int *w, int *h)
{
	*w = g_GetVideoWidth();
	*h = g_GetVideoHeight();
	return (const uint32_t *)g_GetVideoBgra();
}
static const int16_t *core_audio(int *n)
{
	*n = g_GetAudioSampleCount();
	return (const int16_t *)g_GetAudio();
}
static int core_input_was_read(void) { return g_InputWasRead(); }
static int core_domain_count(void) { return g_GetMemoryDomainCount(); }
static int core_vsync_numerator(void) { return g_GetVsyncNumerator(); }
static int core_vsync_denominator(void) { return g_GetVsyncDenominator(); }
static int32_t core_savedata_count(void) { return g_GetSaveDataFileCount(); }
static const char *core_savedata_name(int32_t i) { return (const char *)g_GetSaveDataFileName(i); }
static int64_t core_savedata_size(int32_t i) { return g_GetSaveDataFileSize(i); }
static const uint8_t *core_savedata_buffer(int32_t i) { return (const uint8_t *)g_GetSaveDataFileBuffer(i); }
static const char *core_domain_name(int i) { return (const char *)g_GetMemoryDomainName(i); }
static const uint8_t *core_domain_ptr(int i) { return (const uint8_t *)g_GetMemoryDomainPtr(i); }
static int64_t core_domain_size(int i) { return g_GetMemoryDomainSize(i); }

/* A state, taken and restored the way the engine does it: the core is told
 * before one is taken (StateSaving) and after one is put back (StateLoaded),
 * if it exports those. A driver that left them out would be testing a core
 * the frontend never runs. */
static void take_state(membuf *into)
{
	mb_return r;
	/* CHIMERA_NO_STATE_SAVING=1 leaves the core untold, as it does in the
	 * engine: the control a leg about what StateSaving carries is held to. */
	static int untold = -1;
	if (untold < 0) { const char *e = getenv("CHIMERA_NO_STATE_SAVING"); untold = (e && e[0] && e[0] != '0'); }
	if (g_StateSaving && !untold) g_StateSaving();
	into->len = 0;
	wbx_save_state(g_host, mem_write, (uintptr_t)into, &r);
	if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
}

static void put_state(membuf *from)
{
	mb_return r;
	from->pos = 0;
	wbx_load_state(g_host, mem_read, (uintptr_t)from, &r);
	if (r.error_message[0]) { fprintf(stderr, "load_state: %s\n", r.error_message); exit(1); }
	gl_state_loaded();
	if (g_StateLoaded) g_StateLoaded();
}

static void core_pre_frame(void)
{
	if (!g_rerecord)
		return;
	take_state(&g_state);
	put_state(&g_state);
}

/* the rewind leg's: one state kept across many frames */
static membuf g_kept;
static void core_state_save(void) { take_state(&g_kept); }
static void core_state_load(void) { put_state(&g_kept); }
static uint64_t core_machine_digest(void) { return g_ChimeraMachineDigest(); }

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: run-wbx <core.wbx> <workdir> [options] [--rerecord]\n");
		return 2;
	}
	const char *wbxPath = argv[1];
	const char *workdir = argv[2];
	for (int i = 3; i < argc; i++)
		if (!strcmp(argv[i], "--rerecord")) g_rerecord = 1;

	struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 3, &o))
		return 2;

	FILE *wf = fopen(wbxPath, "rb");
	if (!wf) { perror(wbxPath); return 1; }

	/* matches waterbox.config memoryLayoutMiB: sbrk, sealed, invisible, plain,
	 * mmap. Keep the two in step - a driver with more room than the frontend
	 * gives is a gate that passes where a user's run dies. */
	mb_memory_layout_template layout = { 512u << 20, 16u << 20, 192u << 20, 16u << 20, 2048u << 20 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); return 1; }
	g_host = (mb_host *)r.data;

	/* mount the whole work dir: rom under its real name + slots + settings.
	 * The sol movie may live in the same dir; it is the driver's, not the
	 * guest's, so skip it (and any .tga the screenshot option wrote). */
	DIR *d = opendir(workdir);
	if (!d) { perror(workdir); return 1; }
	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", workdir, de->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		const char *dot = strrchr(de->d_name, '.');
		if (dot && (!strcmp(dot, ".sol") || !strcmp(dot, ".tga") || !strcmp(dot, ".txt")))
			continue;
		FILE *f = fopen(path, "rb");
		if (!f) { perror(path); return 1; }
		freader rd = { f };
		wbx_mount_file(g_host, de->d_name, file_read, (uintptr_t)&rd, false, &r);
		fclose(f);
		if (r.error_message[0]) { fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message); return 1; }
	}
	closedir(d);

	wbx_activate_host(g_host, &r);

	g_Init = (intfn)proc(g_host, "Init");
	g_GetLoadError = (ptrfn)proc(g_host, "GetLoadError");
	g_SetButton = (setfn)proc(g_host, "SetButton");
	g_SetAxis = (setfn)proc(g_host, "SetAxis");
	g_FrameAdvance = (framefn)proc(g_host, "FrameAdvance");
	g_SetRenderingEnabled = (voidfn_i)proc(g_host, "SetRenderingEnabled");
	g_GetVideoBgra = (ptrfn)proc(g_host, "GetVideoBgra");
	g_GetVideoWidth = (intfn)proc(g_host, "GetVideoWidth");
	g_GetVideoHeight = (intfn)proc(g_host, "GetVideoHeight");
	g_GetAudio = (ptrfn)proc(g_host, "GetAudio");
	g_GetAudioSampleCount = (intfn)proc(g_host, "GetAudioSampleCount");
	g_InputWasRead = (intfn)proc(g_host, "InputWasRead");
	g_GetMemoryDomainCount = (intfn)proc(g_host, "GetMemoryDomainCount");
	g_GetMemoryDomainName = (ptrfn_i)proc(g_host, "GetMemoryDomainName");
	g_GetMemoryDomainPtr = (ptrfn_i)proc(g_host, "GetMemoryDomainPtr");
	g_GetMemoryDomainSize = (i64fn_i)proc(g_host, "GetMemoryDomainSize");
	g_GetVsyncNumerator = (intfn)proc(g_host, "GetVsyncNumerator");
	g_GetVsyncDenominator = (intfn)proc(g_host, "GetVsyncDenominator");
	g_GetSaveDataFileCount = (i32fn)proc(g_host, "GetSaveDataFileCount");
	g_GetSaveDataFileName = (ptrfn_i32)proc(g_host, "GetSaveDataFileName");
	g_GetSaveDataFileSize = (i64fn_i32)proc(g_host, "GetSaveDataFileSize");
	g_GetSaveDataFileBuffer = (ptrfn_i32)proc(g_host, "GetSaveDataFileBuffer");
	g_ChimeraMachineDigest = (u64fn)proc(g_host, "ChimeraMachineDigest");
	g_StateSaving = (voidfn)proc_optional(g_host, "StateSaving");
	g_StateLoaded = (voidfn)proc_optional(g_host, "StateLoaded");

	/* The GPU bridge (experiment; see waterbox/gl-bridge.h). Off unless
	 * CHIMERA_GPU=1 asks for it, and even then a machine with no usable driver
	 * simply carries on with the softpipe: this is meant to be faster, not
	 * required. It must be handed over BEFORE Init, because Init is where the
	 * renderer is chosen. */
#ifdef CHIMERA_GL_BRIDGE
	{
		const char *want = getenv("CHIMERA_GPU");
		if (want && strcmp(want, "0") != 0)
		{
			char glerr[256] = "";
			if (chimera_gl_host_init(glerr, sizeof glerr) != 0)
			{
				fprintf(stderr, "gpu bridge: no context (%s); the softpipe is unaffected\n", glerr);
			}
			else
			{
				setfn_v set_bridge = (setfn_v)proc(g_host, "SetGpuBridge");
				uintptr_t guest_addr = 0;
				wbx_get_callback_addr(g_host, (mb_external_callback)chimera_gl_host_dispatch, 0, &r);
				guest_addr = r.data;
				if (!guest_addr || !set_bridge)
				{
					fprintf(stderr, "gpu bridge: could not register the callback\n");
				}
				else
				{
					fprintf(stderr, "gpu bridge: %s\n", chimera_gl_host_description());
					set_bridge((uint64_t)guest_addr);
				}
			}
		}
	}
#endif

	struct gate_core c = {
		.init = core_init,
		.load_error = core_load_error,
		.set_button = core_set_button,
		.frame = core_frame,
		.video = core_video,
		.audio = core_audio,
		.input_was_read = core_input_was_read,
		.domain_count = core_domain_count,
		.domain_name = core_domain_name,
		.domain_ptr = core_domain_ptr,
		.domain_size = core_domain_size,
		.set_axis = core_set_axis,
		.vsync_numerator = core_vsync_numerator,
		.vsync_denominator = core_vsync_denominator,
		.savedata_count = core_savedata_count,
		.savedata_name = core_savedata_name,
		.savedata_size = core_savedata_size,
		.savedata_buffer = core_savedata_buffer,
		.pre_frame = core_pre_frame,
		.set_rendering = core_set_rendering,
		.machine_digest = core_machine_digest,
		.state_save = core_state_save,
		.state_load = core_state_load,
	};

	/* Init runs before Seal - the loaded machine is the sealed baseline */
	if (c.init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c.load_error());
		return 1;
	}

	wbx_deactivate_host(g_host, &r);
	wbx_seal(g_host, &r);
	if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); return 1; }
	wbx_activate_host(g_host, &r);

	c.init = core_init_done;
	int ret = gate_run(&c, &o);
	gl_report_unhandled();

	wbx_deactivate_host(g_host, &r);
	wbx_destroy_host(g_host, &r);
	free(g_state.b);
	return ret;
}
