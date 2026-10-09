/* The files upstream reads out of a "resources" folder beside its executable,
 * for a core that has no folder: assembled in, byte for byte, from the pinned
 * submodule (the include path names extern/duckstation/data/resources).
 *
 * Only what the MACHINE uses is here. gamedb.yaml is the per-title database:
 * which controller a game needs, which hardware quirk it trips over, which
 * settings would break it. A core without it runs every game with none of its
 * own corrections, and nothing looks wrong until one misbehaves.
 */
	.section .rodata
	.balign 16
	.global chimera_res_gamedb
	.global chimera_res_gamedb_end
chimera_res_gamedb:
	.incbin "gamedb.yaml"
chimera_res_gamedb_end:
	.byte 0

	.section .note.GNU-stack,"",@progbits
