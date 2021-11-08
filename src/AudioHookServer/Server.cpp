#include <cstdint>
#include <unordered_set>
#include <deque>
#include <cmath>
#include <algorithm>
#include <bit>

#include <uwebsockets/App.h>
#include <pffft/pffft.h>

#include "Server.h"
#include "Log.h"
#include "AudioHook/AudioCapturer.h"

#if _WIN32
//https://github.com/joyent/libuv/issues/1089#issuecomment-33316312
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "userenv.lib")
#endif

const int SAMPLE_RATE = 44100;
const int NUM_CHANNELS = 2;
const int FRAME_SIZE = 512;
const int TICK_RATE = SAMPLE_RATE / FRAME_SIZE;

class AudioFrameSplitter
{
public:
    AudioFrameSplitter(int size, int channels)
    {
        _frameSize = size;
        _numChannels = channels;
        _fft = pffft_new_setup(size, PFFFT_REAL);

        _pcm = (int16_t*)pffft_aligned_malloc(size * channels * sizeof(int16_t));
        _fftBufIn = (float*)pffft_aligned_malloc(size * sizeof(float));
        _fftBufOut = (float*)pffft_aligned_malloc(size * sizeof(float));
        _fftWindow = (float*)pffft_aligned_malloc(size * sizeof(float));

        for (int i = 0; i < size; i++) {
            //Blackman-Harris window produces very thick peaks, the resulting spectogram looks "blurry"
            //Welch is sharper and it shouldn't be a huge issue for what we want.
            //(Audacity's spectogram Blackman-Harris is actually more detailed in some cases, maybe i'm fucking something up here)
            float w = WelchWindow(i / (size - 1.0f));
            _fftWindow[i] = w / 32768.0f;
        }
    }
    ~AudioFrameSplitter()
    {
        pffft_destroy_setup(_fft);

        pffft_aligned_free(_pcm);
        pffft_aligned_free(_fftBufIn);
        pffft_aligned_free(_fftBufOut);
        pffft_aligned_free(_fftWindow);
    }

    void EnqueuePCM(const int16_t* buf, int count)
    {
        _pcmQueue.insert(_pcmQueue.end(), buf, buf + count * _numChannels);
        //drop old frames if the queue grows too much somehow
        if (_pcmQueue.size() >= std::max(count * 4, _frameSize * _numChannels * 8)) {
            LogWarn("Audio queue has grown too much! Dropping old frames.");
            _pcmQueue.erase(_pcmQueue.begin(), _pcmQueue.begin() + _frameSize * _numChannels);
        }
    }
    bool DequeueFrame(std::string& msg)
    {
        if (!RefillPcmBuffer()) {
            return false;
        }
        int numFreqBins = _frameSize / 2;

        //build message
        msg.resize(0);
        msg.reserve(6 + (2 * _frameSize + 4 * numFreqBins) * _numChannels);
        WriteLE<uint32_t>(msg, _frameSize);
        WriteLE<uint16_t>(msg, _numChannels);

        for (int c = 0; c < _numChannels; c++) {
            int16_t* pcm = _pcm + c * _frameSize;
            ApplyFFT(pcm);
            
            msg.append((char*)pcm, _frameSize * sizeof(int16_t));
            msg.append(msg.size() & 3, '\0'); //align padding
            msg.append((char*)_fftBufOut, numFreqBins * sizeof(float));
        }
        return true;
    }

private:
    std::deque<int16_t> _pcmQueue;
    PFFFT_Setup* _fft;

    int _frameSize, _numChannels;
    int16_t* _pcm;
    float* _fftBufIn;
    float* _fftBufOut;
    float* _fftWindow;

    bool RefillPcmBuffer()
    {
        int totalSamples = _frameSize * _numChannels;
        if (_pcmQueue.size() < totalSamples) {
            return false;
        }
        //dequeue and deinterleave
        //ABABABABAB -> AAAAABBBBB
        auto itr = _pcmQueue.begin();
        for (int i = 0; i < _frameSize; i++) {
            for (int c = 0; c < _numChannels; c++) {
                _pcm[i + c * _frameSize] = *itr++;
            }
        }
        _pcmQueue.erase(_pcmQueue.begin(), itr);
        return true;
    }
    
    void ApplyFFT(int16_t* pcm)
    {
        //convert pcm to float while applying the window
        for (int i = 0; i < _frameSize; i++) {
            _fftBufIn[i] = pcm[i] * _fftWindow[i];
        }
        
        pffft_transform_ordered(_fft, _fftBufIn, _fftBufOut, nullptr, PFFFT_FORWARD);
        //compute magnitude of the result
        for (int i = 0; i < _frameSize / 2; i++) {
            float re = _fftBufOut[i * 2 + 0];
            float im = _fftBufOut[i * 2 + 1];
            _fftBufOut[i] = sqrtf(re * re + im * im);
        }
    }
    
    const float pi = 3.141592653589793f;
    float BlackmanHarrisWindow(float x)
    {
        return 0.35875f -
               0.48829f * cosf((2 * pi) * x) +
               0.14128f * cosf((4 * pi) * x) -
               0.01168f * cosf((6 * pi) * x);
    }
    float HannWindow(float x)
    {
        float w = sinf(pi * x);
        return w*w;
    }
    float WelchWindow(float x)
    {
        x = x * 2.0f - 1.0f;
        return 1.0f - x*x;
    }

    template<typename T>
    void WriteLE(std::string& str, T value)
    {
        static_assert(std::endian::native == std::endian::little);
        str.append((char*)&value, sizeof(T));
    }
};

class ServerImpl
{
public:
    ServerImpl(int targetProcessId) :
        _processId(targetProcessId),
        _audioCap(AudioCapturer::Create(SAMPLE_RATE, NUM_CHANNELS)),
        _frameSplitter(FRAME_SIZE, NUM_CHANNELS)
    {
    }

    void Run()
    {
        _app = new uWS::App();

        _app->ws<Connection>("/audio_hook", {
            .compression = uWS::CompressOptions::DISABLED,
            .maxPayloadLength = 1024 * 1024 * 1,
            .idleTimeout = 32,
            .maxBackpressure = 1024 * 1024 * 8,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = false,
            .sendPingsAutomatically = true,

            .open = [&](WebSocket* ws) {
                LogDebug("Incomming connection from {}@{}", ws->getRemoteAddressAsText(), (void*)ws);
                _conns.insert(ws);
                if (_conns.size() == 1) {
                    _audioCap->Start(_processId);
                }
            },
            .message = [&](WebSocket* ws, std::string_view data, uWS::OpCode msgType) {
                if (msgType != uWS::BINARY || data.size() < 4) {
                    throw std::exception("Message must be binary");
                }
            },
            .close = [&](WebSocket* ws, int code, std::string_view reason) {
                LogDebug("Closing connection from {}@{} (code={} reason={})", ws->getRemoteAddressAsText(), (void*)ws, code, reason);
                _conns.erase(ws);
                if (_conns.size() == 0) {
                    _audioCap->Stop();
                }
            }
        })
        .listen("127.0.0.1", 6347, [&](auto socket) {
            if (!socket) {
                //TODO: try multiple ports
                throw std::exception("Failed to open audio server.");
            }
            _socket = socket;
        });
        //timer thingy: https://github.com/uNetworking/uWebSockets/discussions/1332#discussioncomment-1330175
        auto loop = uWS::Loop::get();
        _timer = us_create_timer((us_loop_t*)loop, 0, sizeof(ServerImpl*));

        *(ServerImpl**)us_timer_ext(_timer) = this;
        us_timer_set(_timer, [](auto timer) {
            (*(ServerImpl**)us_timer_ext(timer))->Tick();
        }, 1, std::max(1, 1000 / TICK_RATE));

        _app->run();
    }
    void Stop()
    {
        if (!_app) return;

        for (auto conn : _conns) {
            conn->close();
            conn->end(1001, "Server is closing");
        }
        _conns.clear();

        us_timer_close(_timer);
        us_listen_socket_close(0, _socket);
        delete _app;
        uWS::Loop::get()->free();
        _app = nullptr;
        _timer = nullptr;
        _socket = nullptr;
    }

private:
    uWS::App* _app = nullptr;
    us_timer_t* _timer = nullptr;
    us_listen_socket_t* _socket = nullptr;

    int _processId;

    struct Connection
    {
    };
    using WebSocket = uWS::WebSocket<false, true, Connection>;

    std::unordered_set<WebSocket*> _conns;

    std::unique_ptr<AudioCapturer> _audioCap;
    AudioFrameSplitter _frameSplitter;

    void Tick()
    {
        const int16_t* batchData;
        int batchSize;
        while (_audioCap->ReceiveBatch(&batchData, &batchSize)) {
            _frameSplitter.EnqueuePCM(batchData, batchSize);
            _audioCap->ConsumeBatch();
        }

        std::string frameData;
        while (_frameSplitter.DequeueFrame(frameData)) {
            /* Message format: (everything little endian)
             * u32 NumSamples
             * u16 NumChannels
             * ChannelData Channels[NumChannels]
             *
             * struct ChannelData {
             *   u16 PCM[NumSamples]
             *   <Alignment Padding of 4>
             *   f32 FFT[NumSamples/2]
             * }
             */
            for (auto* conn : _conns) {
                conn->send(frameData, uWS::BINARY);
            }
        }
    }
};

//TODO: maybe don't have a class
namespace Server
{
    ServerImpl* g_server;

    void Run(int targetProcessId)
    {
        if (g_server) {
            throw std::exception("Server already running");
        }
        g_server = new ServerImpl(targetProcessId);
        g_server->Run();
    }
    void Stop()
    {
        if (g_server) {
            g_server->Stop();
            delete g_server;
            g_server = nullptr;
        }
    }
}