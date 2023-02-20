#include <windows.h>
#include <inttypes.h>
#include <assert.h>

typedef int16_t MonoSample;

struct StereoSample
{
	int16_t left;
	int16_t right;
};

struct Waveform
{
	uint32_t sampleCount;
	MonoSample* monoSamples;
	StereoSample* stereoSamples;
};

struct WaveformHandle
{
	uint64_t id;
	uint32_t index;
};

struct WaveformManager
{
	static const uint32_t maxSlots = 1024;
	struct Slot {
		Waveform* waveform;
		uint64_t id;
	};

	Slot slots[maxSlots];
	uint64_t previousWaveformId;
};

void destroyWaveform(Waveform* waveform)
{
	free(waveform->monoSamples);
	free(waveform->stereoSamples);
}

WaveformHandle addWaveform(WaveformManager* waveformManager, Waveform waveform)
{
	Waveform* waveformPtr = (Waveform*)malloc(sizeof(Waveform));
	memcpy(waveformPtr, &waveform, sizeof(Waveform));
	for (uint32_t i=0; i<WaveformManager::maxSlots; ++i) {
		void* result = InterlockedCompareExchangePointer((void**)&waveformManager->slots[i].waveform, waveformPtr, 0);
		if (result == 0) {
			waveformManager->slots[i].id = InterlockedIncrement(&waveformManager->previousWaveformId);
			WaveformHandle handle = {0};
			handle.id = waveformManager->slots[i].id;
			handle.index = i;
			return handle;
		}
	}
	assert(!"Waveform manager is full; exceeded max loaded waveforms.");
	return {0};
}

Waveform* getWaveform(WaveformManager* waveformManager, WaveformHandle handle)
{
	return (handle.index < waveformManager->maxSlots && waveformManager->slots[handle.index].id == handle.id)? waveformManager->slots[handle.index].waveform : 0;
}

void markWaveformForRemoval(WaveformManager* waveformManager, WaveformHandle handle)
{
	waveformManager->slots[handle.index].id = ULLONG_MAX;
}

void updateWaveformManager(WaveformManager* waveformManager)
{
	for (uint32_t i=0; i<WaveformManager::maxSlots; ++i) {
		if (waveformManager->slots[i].id == ULLONG_MAX) {
			free(waveformManager->slots[i].waveform->monoSamples);
			free(waveformManager->slots[i].waveform->stereoSamples);
			free(waveformManager->slots[i].waveform);
			waveformManager->slots[i].id = 0;
			MemoryBarrier();
			waveformManager->slots[i].waveform = 0;
		}
	}
}
