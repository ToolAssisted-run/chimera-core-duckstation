/* The files upstream reads out of a "resources" folder beside its executable,
 * for a core that has no folder: assembled in, byte for byte, from the pinned
 * submodule (the include path names extern/duckstation/data/resources).
 *
 * Only what the MACHINE uses is here. gamedb.yaml is the per-title database:
 * which controller a game needs, which hardware quirk it trips over, which
 * settings would break it. A core without it runs every game with none of its
 * own corrections, and nothing looks wrong until one misbehaves.
 *
 * This file is assembled twice, once for each emulator (meson.build, "one
 * binary, two machines"). The arcade boards' is ArcadeDuck's own record of
 * them, arcadedb.yaml: which board a rom set is for, what is in the set, what
 * the cabinet's panel has. It has no gamedb.yaml - a board runs one game.
 */
	.section .rodata
	.balign 16
#ifdef CHIMERA_ARCADE
	.global chimera_res_arcadedb
	.global chimera_res_arcadedb_end
chimera_res_arcadedb:
	.incbin "arcadedb.yaml"
chimera_res_arcadedb_end:
	.byte 0
#else
	.global chimera_res_gamedb
	.global chimera_res_gamedb_end
chimera_res_gamedb:
	.incbin "gamedb.yaml"
chimera_res_gamedb_end:
	.byte 0
#endif

	.section .note.GNU-stack,"",@progbits
