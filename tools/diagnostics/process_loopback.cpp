// Process-tree-only Windows audio capture. No system-loopback fallback.
// API contract: Microsoft's ApplicationLoopback sample and WASAPI documentation.
// Samples/packet timestamps stay in bounded memory while recording; disk output
// is written after Stop(), so capture I/O cannot stall the game's audio thread.
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "avrt.lib")
using Microsoft::WRL::ComPtr;

namespace {
constexpr uint32_t rate = 48000, channels = 2, bytes_per_frame = 4;
std::atomic<HANDLE> stop_event{nullptr};
BOOL WINAPI control(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) {
        HANDLE event = stop_event.load();
        if (event) SetEvent(event);
        return TRUE;
    }
    return FALSE;
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
int64_t qpc() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string quote(const std::wstring& value) {
    std::string result = "\"";
    for (unsigned char c : utf8(value)) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { char escaped[8]; sprintf_s(escaped, "\\u%04x", c); result += escaped; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
bool write_all(HANDLE file, const void* data, size_t bytes) {
    const BYTE* next = static_cast<const BYTE*>(data);
    while (bytes) {
        DWORD written = 0, count = static_cast<DWORD>(std::min<size_t>(bytes, 1024 * 1024));
        if (!WriteFile(file, next, count, &written, nullptr) || !written) return false;
        next += written; bytes -= written;
    }
    return true;
}
bool create_output(Handle& file, const std::wstring& path) {
    file.value = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    return file.value != INVALID_HANDLE_VALUE;
}
bool text_file(const std::wstring& path, const std::string& value) {
    Handle file;
    return create_output(file, path) && write_all(file.value, value.data(), value.size());
}
struct Packet {
    uint64_t sample_offset, device_position, qpc_100ns;
    int64_t received_qpc;
    uint32_t frames, flags;
};
class Completion final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    Handle event;
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    Completion() { event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activation = E_UNEXPECTED;
        ComPtr<IUnknown> object;
        result = operation->GetActivateResult(&activation, &object);
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = object.As(&client);
        SetEvent(event.value);
        return S_OK;
    }
};
void fail(const char* stage, HRESULT hr) { fprintf(stderr, "process_loopback: %s failed 0x%08lX\n", stage, static_cast<unsigned long>(hr)); }
bool integer(const wchar_t* text, uint32_t& value) {
    if (!*text) return false;
    wchar_t* end = nullptr;
    unsigned long long result = _wcstoui64(text, &end, 10);
    if (*end || result > UINT32_MAX || result == 0 || *text == L'-') return false;
    value = static_cast<uint32_t>(result); return true;
}
std::wstring full_path(const std::wstring& value) {
    DWORD needed = GetFullPathNameW(value.c_str(), 0, nullptr, nullptr);
    if (!needed) return {};
    std::wstring result(needed, L'\0');
    DWORD count = GetFullPathNameW(value.c_str(), needed, result.data(), nullptr);
    if (!count || count >= needed) return {};
    result.resize(count); return result;
}
int capture(uint32_t pid, uint32_t seconds, const std::wstring& prefix) {
    // A live target handle identifies this process instance and allows exit
    // detection without injecting, suspending or reading its memory.
    Handle target;
    target.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!target.value) { fail("OpenProcess(target)", HRESULT_FROM_WIN32(GetLastError())); return 1; }
    if (WaitForSingleObject(target.value, 0) != WAIT_TIMEOUT) { fail("target already exited", E_INVALIDARG); return 1; }
    wchar_t image[32768]; DWORD image_size = ARRAYSIZE(image);
    if (!QueryFullProcessImageNameW(target.value, 0, image, &image_size)) { fail("QueryFullProcessImageName", HRESULT_FROM_WIN32(GetLastError())); return 1; }
    std::wstring image_path(image, image_size);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { fail("CoInitializeEx", hr); return 1; }
    struct ComScope { ~ComScope() { CoUninitialize(); } } com_scope;
    auto completion = Microsoft::WRL::Make<Completion>();
    if (!completion || !completion->event.value) { fail("completion event", E_OUTOFMEMORY); return 1; }
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    parameters.ProcessLoopbackParams.TargetProcessId = pid;
    parameters.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(parameters);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activation, completion.Get(), &operation);
    if (FAILED(hr)) { fail("process-only ActivateAudioInterfaceAsync (no fallback)", hr); return 1; }
    if (WaitForSingleObject(completion->event.value, 30000) != WAIT_OBJECT_0) { fail("activation completion timeout", HRESULT_FROM_WIN32(ERROR_TIMEOUT)); return 1; }
    if (FAILED(completion->result)) { fail("process-only activation result (requires Windows build20348+)", completion->result); return 1; }
    ComPtr<IAudioClient> client = completion->client;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = channels;
    format.nSamplesPerSec = rate; format.wBitsPerSample = 16;
    format.nBlockAlign = bytes_per_frame; format.nAvgBytesPerSec = rate * bytes_per_frame;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 0, 0, &format, nullptr);
    if (FAILED(hr)) { fail("Initialize(process loopback PCM48k)", hr); return 1; }
    ComPtr<IAudioCaptureClient> reader;
    hr = client->GetService(IID_PPV_ARGS(&reader));
    if (FAILED(hr)) { fail("GetService(IAudioCaptureClient)", hr); return 1; }
    Handle ready, stop;
    ready.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stop.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready.value || !stop.value) { fail("capture events", HRESULT_FROM_WIN32(GetLastError())); return 1; }
    hr = client->SetEventHandle(ready.value);
    if (FAILED(hr)) { fail("SetEventHandle", hr); return 1; }
    // Reserve every byte/packet before Start. No allocations or file writes
    // occur while capturing, apart from the one small ready receipt.
    const uint64_t frame_capacity = static_cast<uint64_t>(seconds + 1) * rate;
    std::vector<int16_t> samples(static_cast<size_t>(frame_capacity * channels));
    std::vector<Packet> packets;
    packets.reserve(static_cast<size_t>(seconds) * 250 + 1024);
    Handle wav, csv, metadata;
    if (!create_output(wav, prefix + L".wav") || !create_output(csv, prefix + L".packets.csv") || !create_output(metadata, prefix + L".json")) {
        fail("reserve output paths (will not overwrite)", HRESULT_FROM_WIN32(GetLastError())); return 1;
    }
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
    DWORD mmcss_error = mmcss ? 0 : GetLastError();
    struct TaskScope { HANDLE task; ~TaskScope() { if (task) AvRevertMmThreadCharacteristics(task); } } task_scope{mmcss};
    stop_event.store(stop.value); SetConsoleCtrlHandler(control, TRUE);
    struct ControlScope { ~ControlScope() { SetConsoleCtrlHandler(control, FALSE); stop_event.store(nullptr); } } control_scope;
    int64_t start_qpc = qpc();
    hr = client->Start();
    if (FAILED(hr)) { fail("Start", hr); return 1; }
    std::string ready_text = "{\"status\":\"capturing\",\"target_pid\":" + std::to_string(pid) + ",\"scope\":\"include_target_process_tree_only\",\"start_qpc\":" + std::to_string(start_qpc) + ",\"qpc_frequency\":" + std::to_string(frequency.QuadPart) + ",\"format\":\"s16le stereo48000\"}\n";
    if (!text_file(prefix + L".ready.json", ready_text)) { client->Stop(); fail("write ready receipt", HRESULT_FROM_WIN32(GetLastError())); return 1; }
    uint64_t frames = 0, discontinuities = 0, silent_packets = 0, timestamp_errors = 0;
    HRESULT capture_result = S_OK;
    const char* reason = "duration";
    auto drain = [&]() -> bool {
        for (;;) {
            UINT32 next = 0;
            capture_result = reader->GetNextPacketSize(&next);
            if (FAILED(capture_result)) return false;
            if (!next) return true;
            BYTE* data = nullptr; UINT32 count = 0; DWORD flags = 0;
            UINT64 position = 0, packet_qpc = 0;
            capture_result = reader->GetBuffer(&data, &count, &flags, &position, &packet_qpc);
            if (FAILED(capture_result)) return false;
            bool fits = frames + count <= frame_capacity && packets.size() < packets.capacity();
            if (fits) {
                packets.push_back({frames, position, packet_qpc, qpc(), count, flags});
                int16_t* destination = samples.data() + frames * channels;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) memset(destination, 0, static_cast<size_t>(count) * bytes_per_frame);
                else if (data) memcpy(destination, data, static_cast<size_t>(count) * bytes_per_frame);
                else fits = false;
                frames += fits ? count : 0;
                discontinuities += (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
                silent_packets += (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                timestamp_errors += (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0;
            }
            HRESULT release = reader->ReleaseBuffer(count);
            if (!fits) { capture_result = E_OUTOFMEMORY; reason = "bounded_storage_limit_or_invalid_packet"; return false; }
            if (FAILED(release)) { capture_result = release; return false; }
        }
    };
    HANDLE events[] = {ready.value, target.value, stop.value};
    while ((qpc() - start_qpc) < static_cast<int64_t>(seconds) * frequency.QuadPart) {
        DWORD wait = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, 100);
        if (wait == WAIT_OBJECT_0 + 1) { reason = "target_exit"; break; }
        if (wait == WAIT_OBJECT_0 + 2) { reason = "control_event"; break; }
        if (wait == WAIT_FAILED) { capture_result = HRESULT_FROM_WIN32(GetLastError()); reason = "event_wait_error"; break; }
        if (!drain()) { if (SUCCEEDED(capture_result)) capture_result = E_FAIL; reason = "capture_error"; break; }
    }
    HRESULT stop_result = client->Stop();
    if (SUCCEEDED(capture_result)) (void)drain();
    int64_t end_qpc = qpc();
    const uint32_t data_bytes = static_cast<uint32_t>(frames * bytes_per_frame);
    BYTE header[44]{};
    auto word = [&](size_t offset, uint16_t value) { memcpy(header + offset, &value, sizeof value); };
    auto dword = [&](size_t offset, uint32_t value) { memcpy(header + offset, &value, sizeof value); };
    memcpy(header, "RIFF", 4); dword(4, data_bytes + 36); memcpy(header + 8, "WAVEfmt ", 8); dword(16, 16);
    word(20, WAVE_FORMAT_PCM); word(22, channels); dword(24, rate); dword(28, rate * bytes_per_frame); word(32, bytes_per_frame); word(34, 16);
    memcpy(header + 36, "data", 4); dword(40, data_bytes);
    bool written = write_all(wav.value, header, sizeof header) && write_all(wav.value, samples.data(), data_bytes);
    std::string table = "sample_offset,frames,flags,device_position,qpc_100ns,received_qpc\n";
    for (const Packet& packet : packets) {
        table += std::to_string(packet.sample_offset) + ',' + std::to_string(packet.frames) + ',' + std::to_string(packet.flags) + ',' + std::to_string(packet.device_position) + ',' + std::to_string(packet.qpc_100ns) + ',' + std::to_string(packet.received_qpc) + '\n';
        if (table.size() > 1024 * 1024) { written = write_all(csv.value, table.data(), table.size()) && written; table.clear(); }
    }
    written = write_all(csv.value, table.data(), table.size()) && written;
    char status[2048];
    sprintf_s(status, ",\"requested_seconds\":%u,\"sample_rate\":48000,\"channels\":2,\"bits_per_sample\":16,\"qpc_frequency\":%lld,\"start_qpc\":%lld,\"end_qpc\":%lld,\"frames\":%llu,\"packets\":%zu,\"discontinuity_packets\":%llu,\"silent_packets\":%llu,\"timestamp_error_packets\":%llu,\"capture_hresult\":%ld,\"stop_hresult\":%ld,\"mmcss_registered\":%s,\"mmcss_error\":%lu,\"stop_reason\":\"%s\",\"data_written\":%s", seconds, frequency.QuadPart, start_qpc, end_qpc, frames, packets.size(), discontinuities, silent_packets, timestamp_errors, capture_result, stop_result, mmcss ? "true" : "false", mmcss_error, reason, written ? "true" : "false");
    std::string receipt = "{\"schema_version\":1,\"scope\":\"include_target_process_tree_only\",\"system_loopback_fallback\":false,\"target_pid\":" + std::to_string(pid) + ",\"target_image\":" + quote(image_path) + ",\"output_prefix\":" + quote(prefix) + status + ",\"limitation\":\"Windows process-render loopback; not a physical speaker/microphone recording. Packet discontinuities may be capture faults and are not alone proof of audible corruption.\"}\n";
    written = write_all(metadata.value, receipt.data(), receipt.size()) && written;
    fprintf(stderr, "process_loopback: %s, %llu frames, %zu packets, discontinuities=%llu; only PID%lu tree\n", reason, frames, packets.size(), discontinuities, static_cast<unsigned long>(pid));
    if (FAILED(capture_result)) fail("capture", capture_result);
    return written && SUCCEEDED(capture_result) && SUCCEEDED(stop_result) ? 0 : 1;
}
}
int wmain(int argc, wchar_t** argv) {
    uint32_t pid = 0, seconds = 0; std::wstring prefix;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--pid") && i + 1 < argc) { if (!integer(argv[++i], pid)) return 2; }
        else if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) { if (!integer(argv[++i], seconds)) return 2; }
        else if (!wcscmp(argv[i], L"--output") && i + 1 < argc) prefix = full_path(argv[++i]);
        else { fwprintf(stderr, L"usage: process_loopback --pid PID --seconds 1..1800 --output ABS_PREFIX\n"); return 2; }
    }
    if (!pid || !seconds || seconds > 1800 || prefix.empty()) { fwprintf(stderr, L"Specify a live target PID, bounded seconds1..1800 and a new output prefix. No system-wide capture mode exists.\n"); return 2; }
    try { return capture(pid, seconds, prefix); }
    catch (const std::bad_alloc&) { fail("bounded capture allocation", E_OUTOFMEMORY); return 1; }
}
