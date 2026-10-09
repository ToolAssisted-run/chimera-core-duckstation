/* chimera-core-duckstation: one binary, two machines.
 *
 * The package is one core.wbx, and it is two emulators: DuckStation, which is
 * the PlayStation, and ArcadeDuck, which is the arcade boards built out of a
 * PlayStation. They began as the same commit and share most of their names,
 * so each is linked into an object of its own in which every name is local
 * but one - a table of this shape (meson.build says how). waterbox/dispatch.c
 * holds the guest ABI the frontend calls and passes each call to the table of
 * the machine the project asked for.
 *
 * The functions are the guest ABI's own, one for one; cinterface.cpp, which is
 * compiled once per tree, fills a table in at its end. */
#ifndef CHIMERA_MACHINE_H
#define CHIMERA_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ChimeraMachine
{
	const char* (*GetLoadError)(void);
	int (*Init)(void);
	void (*SetButton)(int32_t index, int32_t state);
	int (*IsButtonActive)(int32_t index);
	int (*IsAxisActive)(int32_t index);
	void (*SetAxis)(int32_t index, int32_t value);
	void (*FrameAdvance)(uint64_t packed);
	void (*StateLoaded)(void);
	void (*StateSaving)(void);
	void (*SetGpuBridge)(uint64_t addr); /* null in a core with no OpenGL */
	uint64_t (*MachineDigest)(void);
	void (*SetRenderingEnabled)(int on);
	uint32_t* (*GetVideoBgra)(void);
	int (*GetVideoWidth)(void);
	int (*GetVideoHeight)(void);
	int16_t* (*GetAudio)(void);
	int (*GetAudioSampleCount)(void);
	int (*GetVsyncNumerator)(void);
	int (*GetVsyncDenominator)(void);
	int (*InputWasRead)(void);
	int (*GetMemoryDomainCount)(void);
	const char* (*GetMemoryDomainName)(int which);
	uint8_t* (*GetMemoryDomainPtr)(int which);
	int64_t (*GetMemoryDomainSize)(int which);
	int (*GetMemoryDomainWritable)(int which);
	int32_t (*GetSaveDataFileCount)(void);
	const char* (*GetSaveDataFileName)(int32_t i);
	int64_t (*GetSaveDataFileSize)(int32_t i);
	const uint8_t* (*GetSaveDataFileBuffer)(int32_t i);
	int32_t (*GetDriveCount)(void);
	const char* (*GetDriveName)(int32_t i);
	int32_t (*GetDriveLight)(int32_t i);
	int32_t (*GetDriveMediaCount)(int32_t i);
	const char* (*GetDriveMediaName)(int32_t i, int32_t n);
	int32_t (*GetDriveMediaSelected)(int32_t i);
	int32_t (*GetDriveMediaInserted)(int32_t i);
} ChimeraMachine;

/* the console: always there */
extern const ChimeraMachine chimera_machine_console;
/* the arcade boards: in a core built with -Darcade=true */
extern const ChimeraMachine chimera_machine_arcade;

#ifdef __cplusplus
}
#endif

#endif
