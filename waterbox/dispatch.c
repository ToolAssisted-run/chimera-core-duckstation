/* chimera-core-duckstation: the guest ABI, and which machine answers it.
 *
 * The frontend calls these. Each is passed to the machine the project asked
 * for - the console, or one of the arcade boards - through that machine's
 * table (waterbox/machine.h says why there are two and how they are kept
 * apart). Nothing is decided here but that: which.
 *
 * WHICH is the project's "machine" setting, read the first time anything is
 * asked - before Init, because the frontend offers the GPU bridge before it
 * boots the machine, and the bridge belongs to the emulator that will draw.
 * Once chosen it is in guest memory and travels in every state. */
#include <stdint.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>

#include "machine.h"

static const ChimeraMachine* g_machine;

static const ChimeraMachine* machine(void)
{
	if (g_machine)
		return g_machine;

	g_machine = &chimera_machine_console;
#ifdef CHIMERA_HAS_ARCADE
	{
		char which[32] = "playstation";
		wbx_setting_str("machine", which, sizeof(which));
		if (strcmp(which, "playstation") != 0)
			g_machine = &chimera_machine_arcade;
	}
#endif
	return g_machine;
}

ECL_EXPORT const char* GetLoadError(void) { return machine()->GetLoadError(); }
ECL_EXPORT int Init(void) { return machine()->Init(); }
ECL_EXPORT void SetButton(int32_t index, int32_t state) { machine()->SetButton(index, state); }
ECL_EXPORT int IsButtonActive(int32_t index) { return machine()->IsButtonActive(index); }
ECL_EXPORT int IsAxisActive(int32_t index) { return machine()->IsAxisActive(index); }
ECL_EXPORT void SetAxis(int32_t index, int32_t value) { machine()->SetAxis(index, value); }
ECL_EXPORT void FrameAdvance(uint64_t packed) { machine()->FrameAdvance(packed); }
ECL_EXPORT void StateLoaded(void) { machine()->StateLoaded(); }
ECL_EXPORT void StateSaving(void) { machine()->StateSaving(); }
#ifdef CHIMERA_GUEST_GL
/* Exported only by a core that can draw on a GPU: the export being there is
 * how the frontend knows to offer one. */
ECL_EXPORT void SetGpuBridge(uint64_t addr)
{
	if (machine()->SetGpuBridge)
		machine()->SetGpuBridge(addr);
}
#endif
#ifdef CHIMERA_GUEST_VK
/* The same, for Vulkan: a core that exports both is handed both, and draws
 * through the one its Renderer setting names. */
ECL_EXPORT void SetVulkanBridge(uint64_t addr)
{
	if (machine()->SetVulkanBridge)
		machine()->SetVulkanBridge(addr);
}
#endif
ECL_EXPORT uint64_t ChimeraMachineDigest(void) { return machine()->MachineDigest(); }
ECL_EXPORT void SetRenderingEnabled(int on) { machine()->SetRenderingEnabled(on); }
ECL_EXPORT uint32_t* GetVideoBgra(void) { return machine()->GetVideoBgra(); }
ECL_EXPORT int GetVideoWidth(void) { return machine()->GetVideoWidth(); }
ECL_EXPORT int GetVideoHeight(void) { return machine()->GetVideoHeight(); }
ECL_EXPORT int16_t* GetAudio(void) { return machine()->GetAudio(); }
ECL_EXPORT int GetAudioSampleCount(void) { return machine()->GetAudioSampleCount(); }
ECL_EXPORT int GetVsyncNumerator(void) { return machine()->GetVsyncNumerator(); }
ECL_EXPORT int GetVsyncDenominator(void) { return machine()->GetVsyncDenominator(); }
ECL_EXPORT int InputWasRead(void) { return machine()->InputWasRead(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return machine()->GetMemoryDomainCount(); }
ECL_EXPORT const char* GetMemoryDomainName(int which) { return machine()->GetMemoryDomainName(which); }
ECL_EXPORT uint8_t* GetMemoryDomainPtr(int which) { return machine()->GetMemoryDomainPtr(which); }
ECL_EXPORT int64_t GetMemoryDomainSize(int which) { return machine()->GetMemoryDomainSize(which); }
ECL_EXPORT int GetMemoryDomainWritable(int which) { return machine()->GetMemoryDomainWritable(which); }

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return machine()->GetSaveDataFileCount(); }
ECL_EXPORT const char* GetSaveDataFileName(int32_t i) { return machine()->GetSaveDataFileName(i); }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return machine()->GetSaveDataFileSize(i); }
ECL_EXPORT const uint8_t* GetSaveDataFileBuffer(int32_t i) { return machine()->GetSaveDataFileBuffer(i); }

ECL_EXPORT int32_t GetDriveCount(void) { return machine()->GetDriveCount(); }
ECL_EXPORT const char* GetDriveName(int32_t i) { return machine()->GetDriveName(i); }
ECL_EXPORT int32_t GetDriveLight(int32_t i) { return machine()->GetDriveLight(i); }
ECL_EXPORT int32_t GetDriveMediaCount(int32_t i) { return machine()->GetDriveMediaCount(i); }
ECL_EXPORT const char* GetDriveMediaName(int32_t i, int32_t n) { return machine()->GetDriveMediaName(i, n); }
ECL_EXPORT int32_t GetDriveMediaSelected(int32_t i) { return machine()->GetDriveMediaSelected(i); }
ECL_EXPORT int32_t GetDriveMediaInserted(int32_t i) { return machine()->GetDriveMediaInserted(i); }
