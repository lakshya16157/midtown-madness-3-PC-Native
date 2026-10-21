// Bounded real Windows render fixture. A go-file lets capture start first.
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <vector>
#pragma comment(lib, "winmm.lib")
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    int hz = _wtoi(argv[1]), seconds = _wtoi(argv[2]);
    if (hz < 100 || hz > 10000 || seconds < 1 || seconds > 10) return 2;
    DWORD start = GetTickCount();
    while (GetFileAttributesW(argv[3]) == INVALID_FILE_ATTRIBUTES) {
        if (GetTickCount() - start > 30000) return 3;
        Sleep(5);
    }
    std::vector<short> samples(static_cast<size_t>(seconds) * 48000 * 2);
    for (size_t n = 0; n < samples.size() / 2; ++n)
        samples[n * 2] = samples[n * 2 + 1] = static_cast<short>(1024 * sin(6.283185307179586 * hz * static_cast<double>(n) / 48000));
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    HWAVEOUT output;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)) return 4;
    WAVEHDR header{}; header.lpData = reinterpret_cast<char*>(samples.data());
    header.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(short));
    if (waveOutPrepareHeader(output, &header, sizeof header) || waveOutWrite(output, &header, sizeof header)) return 5;
    while (!(header.dwFlags & WHDR_DONE)) Sleep(10);
    waveOutUnprepareHeader(output, &header, sizeof header);
    waveOutClose(output);
    return 0;
}
