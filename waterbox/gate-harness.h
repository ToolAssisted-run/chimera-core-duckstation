/* gate-harness.h - the shared half of run-native.c and run-wbx.c.
 *
 * One replay-and-digest loop over an abstract core interface, so the native
 * reference and the sandboxed core run EXACTLY the same schedule: the same
 * .sol movie (quickerSnes9x's format) or the same pseudo-random pad exercise,
 * digesting video, audio, lag and every memory domain per frame. The two
 * drivers differ only in how the exports are reached.
 *
 * Wire format (waterbox.config button order): 0 Power, 1 Reset, 2 Select,
 * 3 Left Difficulty, 4 Right Difficulty, 5 TV Type, then P1 and P2 x
 * {Up,Down,Left,Right,Button}. quickerStella's .sol writes the console field
 * in its own order; gate_parse_line maps it.
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The wire this core declares: EIGHT slots, in PCSX2's unified-slot order.
 * Unified 0 and 1 are the two physical ports and 2..7 are the six a multitap
 * adds, which is the order a game sees them in.
 *
 * A slot's block is the DualShock 2's seventeen - the d-pad, start and select,
 * the four face buttons, the four shoulders, the two stick clicks and the
 * analog toggle - and the two PHYSICAL ports carry twenty-six more for the
 * instruments and the light gun, so the blocks are NOT the same size.
 * GATE_SLOT_BASE is the only honest way to index one.
 *
 *   0 Up  1 Down  2 Left  3 Right  4 Start  5 Select
 *   6 Square  7 Cross  8 Circle  9 Triangle
 *   10 L1  11 R1  12 L2  13 R2  14 L3  15 R3  16 Analog
 *   17..20 Negcon A B I II          21..22 Strum Up Down
 *   23..27 frets green red yellow blue orange
 *   28..36 pop'n white/yellow/green/blue left, red, blue/green/yellow/white right
 *   37 gun Trigger  38..40 gun A B C  41 gun Offscreen  42 gun Recalibrate
 *
 * and per slot four axes (LX LY RX RY), the two ports six more (Dial, Twist,
 * Whammy, Tilt, Gun X, Gun Y). */
#define GATE_DS2_BTNS 17
#define GATE_PORT_BTNS 43
#define GATE_PORTS 2
#define GATE_SLOTS 8
#define GATE_BTN_COUNT (GATE_PORT_BTNS * GATE_PORTS + GATE_DS2_BTNS * (GATE_SLOTS - GATE_PORTS))
#define GATE_SLOT_BASE(slot) \
	((slot) < GATE_PORTS ? (slot) * GATE_PORT_BTNS \
		: GATE_PORT_BTNS * GATE_PORTS + ((slot) - GATE_PORTS) * GATE_DS2_BTNS)

/* how many scripted presses one run may carry (--press) */
#define GATE_MAX_PRESSES 32

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	void (*set_axis)(int32_t index, int32_t value); /* NULL if the core has no axes */
	int (*vsync_numerator)(void);
	int (*vsync_denominator)(void);
	int32_t (*savedata_count)(void);
	const char *(*savedata_name)(int32_t i);
	int64_t (*savedata_size)(int32_t i);
	const uint8_t *(*savedata_buffer)(int32_t i);
	/* optional per-frame hook (the rerecord leg); may be NULL */
	void (*pre_frame)(void);
	/* optional: turn the core's drawing on and off (the turbo leg); may be NULL */
	void (*set_rendering)(int on);
};

struct gate_opts
{
	long frames;          /* run length; a movie may end earlier padded with idle */
	const char *solPath;  /* NULL = pad-exercise schedule */
	const char *sys;      /* only "snes" exists; kept for CLI symmetry */
	const char *ctl1;     /* none | joypad */
	const char *ctl2;
	const char *screenshotPath; /* optional final-frame .tga */
	int exercise;         /* nonzero: drive P1 with a deterministic pattern */
	const char *dumpDomain;     /* optional: memory domain to dump after the run... */
	const char *dumpPath;       /* ...into this file (the frontend gate compares it) */
	const char *savedataDir;    /* optional: write every savedata export here after the run */
	int exercisePad;      /* also drive this pad number (2..8) with the exercise, or 0 */
	int wiggleAxes;       /* nonzero: drive every axis with a deterministic wander */
	/* Scripted presses: --press <frame>:<count>:<wire index>, repeatable. The
	 * exercise schedule is a fine way to prove input REACHES the machine, and
	 * a poor way to ask a game a question - a title screen wants "left, then
	 * confirm", not a coin toss. The index is the core's own wire order, which
	 * is the order its waterbox.config declares. */
	struct { long first, count; int index; } press[GATE_MAX_PRESSES];
	int presses;
	/* Axes held for the whole run: --hold-axis <index>:<value>, repeatable.
	 * An absolute axis - a light gun's aim - is not something a wander can
	 * ask a question with: "the gun was pointed HERE" is the question, and
	 * the answer is what the machine did about it. */
	struct { int index; int value; } hold[GATE_MAX_PRESSES];
	int holds;
	int turbo;            /* nonzero: draw nothing for the first half of the run */
	long turboSettle;     /* frames to let the picture settle before hashing it */
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

/* deterministic P1 pad pattern, identical in both drivers, so the input
 * path is part of the comparison; Power/Reset stay untouched */
/* One SLOT's worth of pad, driven from a deterministic schedule. `slot` is
 * 0..7 and picks both the block of wires written and a different schedule, so
 * "player 2 held something" is a distinguishable event from "player 1 did". */
static void gate_exercise_slot(long frame, int slot, uint8_t *buttons)
{
	uint64_t x = (uint64_t)(frame + (long)slot * 7919) * 6364136223846793005ULL
		+ 1442695040888963407ULL;
	x ^= x >> 33;
	uint8_t *b = &buttons[GATE_SLOT_BASE(slot)];
	/* the d-pad, the four face buttons, start and select: the ten a DualShock 2
	 * has that every device in every slot also has. The instruments' buttons
	 * belong to devices a port has to be SET to and are left alone here. */
	static const int wires[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	for (int k = 0; k < 10; k++)
		b[wires[k]] = (x >> k) & 1;
	/* holding UP+DOWN or LEFT+RIGHT is not a pad state a real controller
	 * produces and some games misbehave; drop the contradictions */
	if (b[0] && b[1]) b[1] = 0;
	if (b[2] && b[3]) b[3] = 0;
}

static void gate_exercise_pad(long frame, uint8_t *buttons)
{
	gate_exercise_slot(frame, 0, buttons);
}

/* ---- .sol parsing (quickerGPGX's movie format) ---- */

struct gate_sol
{
	char **lines;
	long count;
};

static int gate_sol_load(const char *path, struct gate_sol *sol)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	sol->lines = NULL;
	sol->count = 0;
	long cap = 0;
	char line[256];
	while (fgets(line, sizeof line, f))
	{
		size_t n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
		if (n == 0) continue;
		if (sol->count == cap)
		{
			cap = cap ? cap * 2 : 1024;
			sol->lines = (char **)realloc(sol->lines, (size_t)cap * sizeof(char *));
		}
		sol->lines[sol->count++] = strdup(line);
	}
	fclose(f);
	return 1;
}

/* one quickerStella joypad field -> the wire's pad block at base; the columns
 * U D L R B map 1:1 onto the wire order. A 2600 pad is four directions and one
 * button, and a driving controller uses the same columns (its rotation is the
 * left and right ones), so both parse the same way. */
static int gate_parse_pad(const char *ctl, const char *s, uint8_t *buttons, int base)
{
	static const char cols[5] = { 'U','D','L','R','B' };
	if (strcmp(ctl, "none") == 0)
		return 0;
	if (strcmp(ctl, "joypad") != 0 && strcmp(ctl, "joystick") != 0 && strcmp(ctl, "driving") != 0)
		return -1;
	for (int i = 0; i < 5; i++)
	{
		if (s[i] != '.' && s[i] != cols[i])
			return -1;
		buttons[base + i] = s[i] != '.';
	}
	return 5;
}

/* a full |r s P l r|U D L R B|... line -> wire buttons; 0 on malformed input.
 *
 * quickerStella's console field is the machine's five switches in ITS order -
 * reset, select, power, left difficulty, right difficulty - which is not the
 * wire's order (power first, then the switches, then TV type). The mapping is
 * spelled out here rather than assumed, because a movie that decodes into the
 * wrong switches would still replay, just differently.
 */
static int gate_parse_line(const char *line, const char *sys, const char *ctl1,
	const char *ctl2, uint8_t *buttons)
{
	(void)sys;
	memset(buttons, 0, GATE_BTN_COUNT);
	const char *s = line;
	if (*s++ != '|') return 0;

	/* movie column -> wire index; the wire's TV type has no movie column */
	static const char consoleCols[5] = { 'r', 's', 'P', 'l', 'r' };
	static const int consoleWire[5] = { 1, 2, 0, 3, 4 };
	for (int i = 0; i < 5; i++)
	{
		if (s[i] != '.' && s[i] != consoleCols[i]) return 0;
		buttons[consoleWire[i]] = s[i] != '.';
	}
	s += 5;

	int n;
	if (strcmp(ctl1, "none") != 0)
	{
		if (*s++ != '|') return 0;
		n = gate_parse_pad(ctl1, s, buttons, 6);
		if (n < 0) return 0;
		s += n;
	}
	if (strcmp(ctl2, "none") != 0)
	{
		if (*s++ != '|') return 0;
		n = gate_parse_pad(ctl2, s, buttons, 11);
		if (n < 0) return 0;
		s += n;
	}
	if (*s++ != '|') return 0;

	return *s == 0;
}

static int gate_write_tga(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	uint8_t hdr[18] = { 0 };
	hdr[2] = 2;
	hdr[12] = w & 0xff; hdr[13] = (w >> 8) & 0xff;
	hdr[14] = h & 0xff; hdr[15] = (h >> 8) & 0xff;
	hdr[16] = 32;
	hdr[17] = 0x20;
	fwrite(hdr, 1, 18, f);
	fwrite(bgra, 4, (size_t)w * h, f);
	fclose(f);
	return 1;
}

/* the loop both drivers share; prints the digest block to stdout */
static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	struct gate_sol sol = { NULL, 0 };
	if (o->solPath && !gate_sol_load(o->solPath, &sol))
	{
		fprintf(stderr, "cannot read movie %s\n", o->solPath);
		return 1;
	}

	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error ? c->load_error() : "?");
		return 1;
	}

	long frames = o->frames;
	if (sol.count > 0 && frames < sol.count)
		frames = sol.count;

	uint64_t vh = 0, ah = 0;
	/* the second half of the run, hashed separately: see the turbo hook. The
	 * settle window is for a machine whose picture is built from more than
	 * one frame - an interlaced display weaves two fields - where the first
	 * picture after drawing resumes is half made of a field nobody drew.
	 * Exactly what a savestate load does, and it converges as fast. */
	const long tail = frames / 2;
	const long hashFrom = tail + o->turboSettle;
	uint64_t th = 0;
	long lag = 0;
	uint8_t buttons[GATE_BTN_COUNT];
	uint8_t prev[GATE_BTN_COUNT];
	memset(prev, 0, sizeof prev);

	for (long f = 0; f < frames; f++)
	{
		memset(buttons, 0, sizeof buttons);
		if (f < sol.count)
		{
			if (!gate_parse_line(sol.lines[f], o->sys, o->ctl1, o->ctl2, buttons))
			{
				fprintf(stderr, "bad movie line %ld: '%s'\n", f, sol.lines[f]);
				return 1;
			}
		}
		else if (o->exercise)
		{
			gate_exercise_pad(f, buttons);
			/* ...and one more slot, on its own schedule. This is how "player 2
			 * held something and player 1 did not" becomes a machine state
			 * different from the other way round, which is the only way to show
			 * that a slot's input reaches THAT slot. */
			if (o->exercisePad >= 2 && o->exercisePad <= GATE_SLOTS)
				gate_exercise_slot(f, o->exercisePad - 1, buttons);
		}

		for (int pi = 0; pi < o->presses; pi++)
		{
			if (f >= o->press[pi].first && f < o->press[pi].first + o->press[pi].count
				&& o->press[pi].index >= 0 && o->press[pi].index < GATE_BTN_COUNT)
			{
				buttons[o->press[pi].index] = 1;
			}
		}

		for (int hi = 0; hi < o->holds && c->set_axis; hi++)
			c->set_axis(o->hold[hi].index, o->hold[hi].value);

		if (o->wiggleAxes && c->set_axis)
		{
			/* a deterministic wander: mouse deltas -2..2, gun coords circling
			 * the middle of the screen; the same values in both drivers */
			uint64_t w = (uint64_t)f * 2862933555777941757ULL + 3037000493ULL;
			w ^= w >> 29;
			c->set_axis(0, (int32_t)(w % 5) - 2);
			c->set_axis(1, (int32_t)((w >> 8) % 5) - 2);
			c->set_axis(2, 96 + (int32_t)((w >> 16) % 64));
			c->set_axis(3, 88 + (int32_t)((w >> 24) % 64));
			c->set_axis(4, 96 + (int32_t)((w >> 32) % 64));
			c->set_axis(5, 88 + (int32_t)((w >> 40) % 64));
			/* the device buttons (wire 98..107) ride the same wander */
			for (int k = 98; k < GATE_BTN_COUNT; k++)
				buttons[k] = (w >> (48 + (k - 98))) & 1;
		}

		/* turbo: draw nothing for the first half of the run, then draw the
		 * second half normally. The second half's pictures are what the
		 * turbo leg compares - one final frame would not do, because a
		 * console with a 3D chip renders when the game submits a list and
		 * not on every frame. */
		if (o->turbo && c->set_rendering)
			c->set_rendering(f >= tail);

		if (c->pre_frame)
			c->pre_frame();

		for (int i = 0; i < GATE_BTN_COUNT; i++)
		{
			if (buttons[i] != prev[i])
				c->set_button(i, buttons[i]);
			prev[i] = buttons[i];
		}

		c->frame();

		int w = 0, h = 0, n = 0;
		const uint32_t *video = c->video(&w, &h);
		const int16_t *audio = c->audio(&n);
		vh = gate_fnv(vh, &w, sizeof w);
		vh = gate_fnv(vh, &h, sizeof h);
		vh = gate_fnv(vh, video, (size_t)w * h * 4);
		if (f >= hashFrom)
		{
			th = gate_fnv(th, &w, sizeof w);
			th = gate_fnv(th, &h, sizeof h);
			th = gate_fnv(th, video, (size_t)w * h * 4);
		}
		ah = gate_fnv(ah, audio, (size_t)n * 2 * sizeof(int16_t));
		if (!c->input_was_read())
			lag++;
		if (o->screenshotPath && f == frames - 1)
			gate_write_tga(o->screenshotPath, video, w, h);

		/* A diagnostic, not a gate: CHIMERA_SHOT_DIR=<dir> writes every frame
		 * from CHIMERA_SHOT_FROM on, so a picture can be compared with the one
		 * before it without replaying the machine once per screenshot. */
		{
			static const char *shotDir = NULL;
			static long shotFrom = 0;
			static int shotInit = 0;
			if (!shotInit)
			{
				shotInit = 1;
				shotDir = getenv("CHIMERA_SHOT_DIR");
				const char *from = getenv("CHIMERA_SHOT_FROM");
				shotFrom = from ? atol(from) : 0;
			}
			if (shotDir && f >= shotFrom)
			{
				char path[512];
				snprintf(path, sizeof path, "%s/f%06ld.tga", shotDir, f);
				gate_write_tga(path, video, w, h);
			}
		}
	}

	printf("frames=%ld\n", frames);
	printf("vsync=%d/%d\n", c->vsync_numerator(), c->vsync_denominator());
	printf("videoHash=%016llx\n", (unsigned long long)vh);
	printf("tailVideoHash=%016llx\n", (unsigned long long)th);
	printf("audioHash=%016llx\n", (unsigned long long)ah);
	printf("lagFrames=%ld\n", lag);
	int nd = c->domain_count();
	for (int i = 0; i < nd; i++)
	{
		uint64_t dh = gate_fnv(0, c->domain_ptr(i), (size_t)c->domain_size(i));
		printf("domain[%s]=%016llx\n", c->domain_name(i), (unsigned long long)dh);
	}
	if (o->savedataDir)
	{
		int32_t count = c->savedata_count();
		printf("savedataFiles=%d\n", count);
		for (int32_t i = 0; i < count; i++)
		{
			char path[4096];
			snprintf(path, sizeof path, "%s/%s", o->savedataDir, c->savedata_name(i));
			FILE *f = fopen(path, "wb");
			if (!f) { perror(path); return 1; }
			fwrite(c->savedata_buffer(i), 1, (size_t)c->savedata_size(i), f);
			fclose(f);
		}
	}

	if (o->dumpDomain && o->dumpPath)
	{
		int found = 0;
		for (int i = 0; i < nd; i++)
		{
			if (strcmp(c->domain_name(i), o->dumpDomain) != 0)
				continue;
			FILE *f = fopen(o->dumpPath, "wb");
			if (!f) { perror(o->dumpPath); return 1; }
			fwrite(c->domain_ptr(i), 1, (size_t)c->domain_size(i), f);
			fclose(f);
			found = 1;
			break;
		}
		if (!found)
		{
			fprintf(stderr, "no such domain to dump: %s\n", o->dumpDomain);
			return 1;
		}
	}
	return 0;
}

/* shared CLI parsing: returns the index of the first positional argument's
 * slot usage message on error */
static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	o->frames = 600;
	o->solPath = NULL;
	o->sys = "snes";
	o->ctl1 = "joypad";
	o->ctl2 = "none";
	o->screenshotPath = NULL;
	o->exercise = 0;
	o->dumpDomain = NULL;
	o->dumpPath = NULL;
	o->savedataDir = NULL;
	o->exercisePad = 0;
	o->wiggleAxes = 0;
	o->presses = 0;
	o->holds = 0;
	o->turbo = 0;
	o->turboSettle = 0;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--sol") && i + 1 < argc) o->solPath = argv[++i];
		else if (!strcmp(argv[i], "--sys") && i + 1 < argc) o->sys = argv[++i];
		else if (!strcmp(argv[i], "--ctl1") && i + 1 < argc) o->ctl1 = argv[++i];
		else if (!strcmp(argv[i], "--ctl2") && i + 1 < argc) o->ctl2 = argv[++i];
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) o->screenshotPath = argv[++i];
		else if (!strcmp(argv[i], "--exercise")) o->exercise = 1;
		else if (!strcmp(argv[i], "--dump-domain") && i + 2 < argc) { o->dumpDomain = argv[++i]; o->dumpPath = argv[++i]; }
		else if (!strcmp(argv[i], "--savedata-out") && i + 1 < argc) o->savedataDir = argv[++i];
		else if (!strcmp(argv[i], "--exercise-pad") && i + 1 < argc) o->exercisePad = (int)strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--wiggle-axes")) o->wiggleAxes = 1;
		else if (!strcmp(argv[i], "--press") && i + 1 < argc)
		{
			if (o->presses >= GATE_MAX_PRESSES) { fprintf(stderr, "too many --press\n"); return 0; }
			long first = 0, count = 0; int index = -1;
			if (sscanf(argv[++i], "%ld:%ld:%d", &first, &count, &index) != 3)
			{
				fprintf(stderr, "--press wants <frame>:<count>:<wire index>\n");
				return 0;
			}
			o->press[o->presses].first = first;
			o->press[o->presses].count = count;
			o->press[o->presses].index = index;
			o->presses++;
		}

		else if (!strcmp(argv[i], "--hold-axis") && i + 1 < argc)
		{
			if (o->holds >= GATE_MAX_PRESSES) { fprintf(stderr, "too many --hold-axis\n"); return 0; }
			int index = -1, value = 0;
			if (sscanf(argv[++i], "%d:%d", &index, &value) != 2)
			{
				fprintf(stderr, "--hold-axis wants <axis index>:<value>\n");
				return 0;
			}
			o->hold[o->holds].index = index;
			o->hold[o->holds].value = value;
			o->holds++;
		}
		else if (!strcmp(argv[i], "--turbo")) o->turbo = 1;
		else if (!strcmp(argv[i], "--turbo-settle") && i + 1 < argc) o->turboSettle = strtol(argv[++i], 0, 0);		else if (!strcmp(argv[i], "--rerecord")) ; /* run-wbx's; ignored here */
		else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 0; }
	}
	return 1;
}

#endif /* GATE_HARNESS_H */
