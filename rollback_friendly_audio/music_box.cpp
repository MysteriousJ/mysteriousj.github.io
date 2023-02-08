// Compile: cl music_box.cpp /link ole32.lib
// Usage:   music_box.exe "Hello World"
#include <windows.h>
#include <inttypes.h>
#include <math.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#define BYTES_PER_CHANNEL sizeof(float)
#define CHANNELS_PER_SAMPLE 1
#define SAMPLES_PER_SECOND 44100

// Public API -----------------------------------------------------------------
struct AudioEvents;
struct AudioDevice;
struct AudioBuffer;
AudioEvents createAudioEvents ();
void        destroyAudioEvents(AudioEvents* events);
void        endAudioEvents    (AudioEvents* events);
AudioDevice createAudioDevice (AudioEvents* events);
void        destroyAudioDevice(AudioDevice* device);
bool        waitForAudioBuffer(AudioDevice* device, AudioEvents* events, AudioBuffer* out_buffer);
void        submitAudioBuffer (AudioDevice* device, AudioBuffer buffer);

// Implementation -------------------------------------------------------------
struct AudioEvents
{
	volatile bool quit = false;
	HANDLE wakeAudioThreadEvent;
};

struct AudioNotifications : public IMMNotificationClient
{
	// Boiler plate
	ULONG STDMETHODCALLTYPE AddRef() { return 1; }
	ULONG STDMETHODCALLTYPE Release() { return 1; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface) { return S_OK; }
	HRESULT OnDeviceAdded(LPCWSTR pwstrDeviceId) { return S_OK; }
	HRESULT OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return S_OK; }
	HRESULT OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) { return S_OK; }
	HRESULT OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) { return S_OK; }

	// Parts that matter
	volatile bool deviceChanged = false;
	HANDLE wakeAudioThreadEvent;

	HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
		if (flow == eRender && role == eMultimedia) {
			deviceChanged = true;
			SetEvent(wakeAudioThreadEvent);
		}
		return S_OK;
	}
};

struct AudioDevice
{
	uint32_t bufferSampleCount;
	AudioNotifications* notifications;
	IMMDeviceEnumerator* enumerator;
	IMMDevice* endpoint;
	IAudioClient* audioClient;
	IAudioRenderClient* renderClient;
};

struct AudioBuffer
{
	float* buffer;
	uint32_t sampleCount;
	uint32_t elapsedSampleTime; // Same as sampleCount when a device is connected
};

AudioEvents createAudioEvents()
{
	AudioEvents events = {0};
	events.wakeAudioThreadEvent = CreateEventA(0, false, false, 0);
	return events;
}

void endAudioEvents(AudioEvents* events)
{
	events->quit = true;
	SetEvent(events->wakeAudioThreadEvent);
}

void destroyAudioEvents(AudioEvents* events)
{
	CloseHandle(events->wakeAudioThreadEvent);
}

AudioDevice createAudioDevice(AudioEvents* events)
{
	AudioDevice device = {0};

	CoCreateInstance( __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&device.enumerator);
	device.notifications = new AudioNotifications;
	device.notifications->wakeAudioThreadEvent = events->wakeAudioThreadEvent;
	device.enumerator->RegisterEndpointNotificationCallback(device.notifications);

	if (SUCCEEDED(device.enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device.endpoint))) {
		if (SUCCEEDED(device.endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&device.audioClient))) {
			REFERENCE_TIME defaultDevicePeriod;
			REFERENCE_TIME minDevicePeriod;
			device.audioClient->GetDevicePeriod(&defaultDevicePeriod, &minDevicePeriod);

			WAVEFORMATEX customFormat = {0};
			customFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			customFormat.nChannels = CHANNELS_PER_SAMPLE;
			customFormat.nSamplesPerSec = SAMPLES_PER_SECOND;
			customFormat.wBitsPerSample = BYTES_PER_CHANNEL*8;
			customFormat.nBlockAlign = (CHANNELS_PER_SAMPLE*BYTES_PER_CHANNEL);
			customFormat.nAvgBytesPerSec = SAMPLES_PER_SECOND*customFormat.nBlockAlign;
			device.audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, defaultDevicePeriod, 0, &customFormat, 0);
			device.audioClient->SetEventHandle(events->wakeAudioThreadEvent);
			device.audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&device.renderClient);
			device.audioClient->GetBufferSize(&device.bufferSampleCount);
			device.audioClient->Start();
		}
	}

	return device;
}

void destroyAudioDevice(AudioDevice* device)
{
	if (device->enumerator) {
		device->enumerator->UnregisterEndpointNotificationCallback(device->notifications);
		device->enumerator->Release();
	}
	if (device->notifications) delete device->notifications;
	if (device->renderClient) device->renderClient->Release();
	if (device->audioClient) device->audioClient->Release();
	if (device->endpoint) device->endpoint->Release();
}

bool waitForAudioBuffer(AudioDevice* device, AudioEvents* events, AudioBuffer* out_buffer)
{
	DWORD waitResult = WaitForSingleObject(events->wakeAudioThreadEvent, 1000);
	if (events->quit) return false;

	if (device->notifications->deviceChanged) {
		destroyAudioDevice(device);
		*device = createAudioDevice(events);
	}

	memset(out_buffer, 0, sizeof(AudioBuffer));
	if (device->renderClient) {
		uint32_t paddingSampleCount; 
		device->audioClient->GetCurrentPadding(&paddingSampleCount);
		uint32_t sampleCount = device->bufferSampleCount - paddingSampleCount;
		float* buffer;
		if (SUCCEEDED(device->renderClient->GetBuffer(sampleCount, (BYTE**)&buffer))) {
			out_buffer->buffer = buffer;
			out_buffer->sampleCount = sampleCount;
			out_buffer->elapsedSampleTime = sampleCount;
			memset(out_buffer->buffer, 0, BYTES_PER_CHANNEL*CHANNELS_PER_SAMPLE*sampleCount);
		}
	}
	else if (waitResult == WAIT_TIMEOUT) {
		out_buffer->elapsedSampleTime = SAMPLES_PER_SECOND;
	}

	return true;
}

void submitAudioBuffer(AudioDevice* device, AudioBuffer buffer)
{
	if (device->renderClient) device->renderClient->ReleaseBuffer(buffer.sampleCount, 0);
}

// Example Program -------------------------------------------------------------
struct AudioThreadData
{
	AudioEvents events;
	char sound;
};

DWORD WINAPI audioThread(void* param)
{
	AudioThreadData* data = (AudioThreadData*)param;

	AudioDevice device = createAudioDevice(&data->events);
	float t = 0;
	float volume = 1;
	float toneHerz = 0;

	AudioBuffer buffer = {0};
	while (waitForAudioBuffer(&device, &data->events, &buffer)) {
		// Sine wave
		for (uint32_t i=0; i<buffer.sampleCount; ++i) {
			// Smoothly turn audio on and off so there's not clicks
			if (data->sound == ' ') {
				volume = max(0, volume - 0.01f);
			}
			else {
				toneHerz = data->sound * data->sound / 8;
				volume = min(1, volume + 0.01f);
			}
			// Render the sound
			buffer.buffer[i] = volume * sinf(t*3.14159f/SAMPLES_PER_SECOND);
			t += toneHerz;
		}
		submitAudioBuffer(&device, buffer);
	}
	destroyAudioDevice(&device);
	return 0;
}

int main(int argc, char** argv)
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	AudioThreadData threadData = {0};
	threadData.events = createAudioEvents();
	HANDLE threadHandle = CreateThread(0, 0, audioThread, &threadData, 0, 0);

	for (int i=0; i<strlen(argv[1]); ++i) {
		threadData.sound = argv[1][i];
		Sleep(150);
	}

	endAudioEvents(&threadData.events);
	WaitForSingleObject(threadHandle, INFINITE);
	destroyAudioEvents(&threadData.events);
	return 0;
}