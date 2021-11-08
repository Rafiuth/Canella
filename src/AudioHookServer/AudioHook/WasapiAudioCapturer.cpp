//Audio capturer based on WASAPI AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
//https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/ApplicationLoopback
//https://github.com/bozbez/win-capture-audio/blob/main/src/audio-capture-helper.cpp

#pragma comment(lib, "mmdevapi.lib")

#include <iostream>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>

#include <wil/com.h>
#include <wrl/implements.h>

#include "AudioCapturer.h"

using namespace Microsoft::WRL;

struct EventCompletionHandler :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler>
{
    wil::unique_event FinishEvent;
    IUnknown* ResultObj;
    HRESULT ResultCode;

    EventCompletionHandler()
    {
        FinishEvent.create();
        ResultObj = nullptr;
        ResultCode = 0;
    }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op)
    {
        HRESULT res = op->GetActivateResult(&ResultCode, &ResultObj);
        if (FAILED(res)) {
            ResultCode = res;
        }
        FinishEvent.SetEvent();

        return S_OK;
    }

    void WaitAndGetResult(IUnknown** obj)
    {
        FinishEvent.wait();
        THROW_IF_FAILED(ResultCode);
        *obj = ResultObj;
    }
};

class WasapiAudioCapturer :
    public AudioCapturer
{
public:
    WasapiAudioCapturer(int sampleRate, int channels)
    {
        _captureFmt = {
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = (WORD)channels,
            .nSamplesPerSec = (DWORD)sampleRate,
            .nAvgBytesPerSec = (DWORD)(sampleRate * channels * sizeof(int16_t)),
            .nBlockAlign = (UWORD)(channels * sizeof(int16_t)),
            .wBitsPerSample = sizeof(int16_t) * 8
        };
    }

    void Start(int processId)
    {
        if (_client != nullptr) {
            //TODO: allow changing processId
            _client->Start();
            return;
        }
        THROW_IF_FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED));

        AUDIOCLIENT_ACTIVATION_PARAMS ap = {
            .ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK,
            .ProcessLoopbackParams = {
                .TargetProcessId = (DWORD)processId,
                .ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
            }
        };
        PROPVARIANT activateParams = {};
        activateParams.vt = VT_BLOB;
        activateParams.blob.cbSize = sizeof(ap);
        activateParams.blob.pBlobData = (BYTE*)&ap;

        EventCompletionHandler activationHandler = {};
        wil::com_ptr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        THROW_IF_FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams, &activationHandler, &asyncOp));
        activationHandler.WaitAndGetResult(_client.put_unknown());

        THROW_IF_FAILED(_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            0, 0, &_captureFmt, NULL
        ));
        THROW_IF_FAILED(_client->GetService(IID_PPV_ARGS(&_captureClient)));

        _readEvent.create();
        _client->SetEventHandle(_readEvent.get());
        _client->Start();
    }
    void Stop()
    {
        if (_client) {
            _client->Stop();
        }
    }

    bool ReceiveBatch(const int16_t** buffer, int* numSamples)
    {
        if (!_captureClient) {
            return false;
        }
        if (_lastBatchData) {
            throw std::exception("ReceiveBatch() called twice before ConsumeBatch()");
        }
        UINT32 availSamples;
        THROW_IF_FAILED(_captureClient->GetNextPacketSize(&availSamples));

        if (availSamples <= 0) {
            return false;
        }
        BYTE* data;
        DWORD flags;
        THROW_IF_FAILED(_captureClient->GetBuffer(&data, &availSamples, &flags, NULL, NULL));

        *buffer = (int16_t*)data;
        *numSamples = (int)availSamples;

        _lastBatchData = data;
        _lastBatchFrames = availSamples;
        return true;
    }
    void ConsumeBatch()
    {
        if (!_lastBatchData) {
            throw std::exception("ConsumeBatch() can only be called after ReceiveBatch()");
        }
        _lastBatchData = nullptr;
        THROW_IF_FAILED(_captureClient->ReleaseBuffer(_lastBatchFrames));
    }
    void WaitForData(int timeoutMs)
    {
        _readEvent.wait(timeoutMs < 0 ? INFINITE : timeoutMs);
    }
private:
    wil::com_ptr<IAudioClient> _client;
    wil::com_ptr<IAudioCaptureClient> _captureClient;
    wil::unique_event _readEvent;

    WAVEFORMATEX _captureFmt;
    BYTE* _lastBatchData;
    DWORD _lastBatchFrames;
};

std::unique_ptr<AudioCapturer> AudioCapturer::Create(int sampleRate, int channels)
{
    return std::make_unique<WasapiAudioCapturer>(sampleRate, channels);
}