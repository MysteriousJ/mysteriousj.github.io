// Compile: cl minimal_wasapi.cpp /link ole32.lib
#include <windows.h>
#include <inttypes.h>
#include <math.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#define BYTES_PER_CHANNEL sizeof(float)
#define CHANNELS_PER_SAMPLE 1
#define SAMPLES_PER_SECOND 44100

int main()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

	IMMDeviceEnumerator* enumerator;
	CoCreateInstance( __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);

	IMMDevice* endpoint;
	enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &endpoint);

	IAudioClient* audioClient;
	endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);

	REFERENCE_TIME defaultDevicePeriod;
	REFERENCE_TIME minDevicePeriod;
	audioClient->GetDevicePeriod(&defaultDevicePeriod, &minDevicePeriod);

	WAVEFORMATEX format = {0};
	format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	format.nChannels = CHANNELS_PER_SAMPLE;
	format.nSamplesPerSec = SAMPLES_PER_SECOND;
	format.wBitsPerSample = BYTES_PER_CHANNEL*8;
	format.nBlockAlign = (CHANNELS_PER_SAMPLE*BYTES_PER_CHANNEL);
	format.nAvgBytesPerSec = SAMPLES_PER_SECOND*format.nBlockAlign;
	audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, defaultDevicePeriod, 0, &format, 0);

	IAudioRenderClient* renderClient;
	audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
	uint32_t bufferSampleCount;
	audioClient->GetBufferSize(&bufferSampleCount);

	uint64_t sampleTime = 0;
	audioClient->Start();

	while (1)
	{
		uint32_t paddingSampleCount; 
		audioClient->GetCurrentPadding(&paddingSampleCount);

		uint32_t sampleCount = bufferSampleCount - paddingSampleCount;
		float* buffer;
		if (SUCCEEDED(renderClient->GetBuffer(sampleCount, (BYTE**)&buffer)))
		{
			// Sine wave
			float toneHerz = 500;
			for (uint32_t i=0; i<sampleCount; ++i) {
				buffer[i] = sinf(sampleTime*3.14159f*toneHerz/SAMPLES_PER_SECOND);
				sampleTime += 1;
			}
			renderClient->ReleaseBuffer(sampleCount, 0);
		}
	}
}