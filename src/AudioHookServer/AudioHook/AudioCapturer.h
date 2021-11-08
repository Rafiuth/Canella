#pragma once
#include <memory>

class AudioCapturer
{
public:
    virtual void Start(int processId) = 0;
    virtual void Stop() = 0;

    /**
     * @brief Attempts to receive the next batch of audio samples.
     * @note This method cannot be used again until AdvanceBatch() is called.
     */
    virtual bool ReceiveBatch(const int16_t** buffer, int* numSamples) = 0;
    virtual void ConsumeBatch() = 0;

    /**
     * @brief Blocks until data is available to be received.
     * @param timeoutMs Timeout in milliseconds or a negative number to indicate infinite.
     */
    virtual void WaitForData(int timeoutMs = -1) = 0;

    static std::unique_ptr<AudioCapturer> Create(int sampleRate, int channels);
};