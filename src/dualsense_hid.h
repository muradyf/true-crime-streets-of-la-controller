// Minimal DualSense (PS5) reader/writer over Windows HID. USB and Bluetooth.
// Report layouts follow the Linux hid-playstation driver:
//   input  USB 0x01 (64 bytes): data starts at byte 1
//   input  BT  0x31 (78 bytes): data starts at byte 2 (sent after feature report 0x05 has been read)
//   input  BT  0x01 ("simple", before that): sticks r[1..4], buttons r[5..7], triggers r[8..9]
//   output USB 0x02 (63 bytes): common block at byte 1
//   output BT  0x31 (78 bytes): seq/tag at 1..2, common block at byte 3, CRC32 (seed byte 0xA2) at 74..77
#pragma once
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>
#include <cstdint>
#include <cstring>
#include <string>

struct PadState {
    bool connected = false;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;   // 0..255, 128 = centre, up = 0
    uint8_t l2 = 0, r2 = 0;                           // 0..255
    uint8_t hat = 8;                                  // 0=N,1=NE,...,7=NW, 8=neutral
    bool square = false, cross = false, circle = false, triangle = false;
    bool l1 = false, r1 = false, l2b = false, r2b = false;
    bool create = false, options = false, l3 = false, r3 = false;
    bool ps = false, touchpad = false, mute = false;
    uint8_t reportId = 0;
    int reportLen = 0;
    bool bluetooth = false;
};

// Output state: rumble motors, lightbar, player LEDs and the two adaptive trigger effects.
struct PadOutput {
    uint8_t motorLeft = 0, motorRight = 0;            // left = strong/low-frequency, right = weak/high-frequency
    uint8_t red = 0, green = 0, blue = 0;
    uint8_t playerLeds = 0;
    uint8_t rightTrigger[11] = {};                    // mode + 10 parameters (0 = off)
    uint8_t leftTrigger[11] = {};
    bool operator==(const PadOutput& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
    bool operator!=(const PadOutput& o) const { return !(*this == o); }
};

class DualSense {
public:
    ~DualSense() { Close(); }

    bool Open() {
        Close();
        GUID hidGuid; HidD_GetHidGuid(&hidGuid);
        HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return false;
        SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
        bool found = false;
        for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
            auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(need);
            if (!det) continue;
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) {
                HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES a{ sizeof(a) };
                    PHIDP_PREPARSED_DATA pp = nullptr; HIDP_CAPS caps{};
                    bool ok = HidD_GetAttributes(h, &a) && a.VendorID == 0x054C &&
                              (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2) &&
                              HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS &&
                              caps.UsagePage == 0x01 && caps.Usage == 0x05;   // gamepad collection
                    if (pp) HidD_FreePreparsedData(pp);
                    if (ok) {
                        handle_ = h; inLen_ = caps.InputReportByteLength; featLen_ = caps.FeatureReportByteLength;
                        outLen_ = caps.OutputReportByteLength;
                        path_ = det->DevicePath;
                        bluetooth_ = inLen_ > 64;   // USB input reports are 64 bytes, Bluetooth 78
                        found = true;
                    } else {
                        CloseHandle(h);
                    }
                }
            }
            free(det);
        }
        SetupDiDestroyDeviceInfoList(set);
        if (!found) return false;
        ov_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wov_.hEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        if (bluetooth_) RequestFullReports();
        lightbarReleased_ = false;
        return true;
    }

    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) { CancelIo(handle_); CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
        if (ov_.hEvent) { CloseHandle(ov_.hEvent); ov_.hEvent = nullptr; }
        if (wov_.hEvent) { CloseHandle(wov_.hEvent); wov_.hEvent = nullptr; }
        pending_ = false; writePending_ = false;
    }

    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool IsBluetooth() const { return bluetooth_; }
    const std::wstring& Path() const { return path_; }
    int FeatureResult() const { return featResult_; }
    DWORD FeatureError() const { return featError_; }
    USHORT FeatureLength() const { return featLen_; }
    USHORT OutputLength() const { return outLen_; }
    DWORD LastWriteError() const { return writeError_; }

    // Waits (up to ms) for an in-flight write and returns its Win32 result (0 = accepted, WAIT_TIMEOUT if still pending).
    DWORD WaitWriteResult(DWORD ms) {
        if (!writePending_) return writeError_;
        if (WaitForSingleObject(wov_.hEvent, ms) != WAIT_OBJECT_0) return WAIT_TIMEOUT;
        DWORD n = 0;
        writeError_ = GetOverlappedResult(handle_, &wov_, &n, FALSE) ? 0 : GetLastError();
        writePending_ = false;
        return writeError_;
    }

    // Non-blocking: drains queued reports, keeps the newest. Returns false if the device went away.
    bool Poll(PadState& st) {
        if (!IsOpen()) return false;
        for (int n = 0; n < 32; ++n) {
            if (!pending_) {
                ResetEvent(ov_.hEvent);
                DWORD got = 0;
                if (ReadFile(handle_, buf_, inLen_, &got, &ov_)) { Parse(buf_, (int)got, st); continue; }
                if (GetLastError() != ERROR_IO_PENDING) { Close(); st.connected = false; return false; }
                pending_ = true;
            }
            DWORD got = 0;
            if (!GetOverlappedResult(handle_, &ov_, &got, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) break;   // nothing new yet
                Close(); st.connected = false; return false;
            }
            pending_ = false;
            Parse(buf_, (int)got, st);
        }
        return true;
    }

    // Non-blocking write of the full output state. Returns false if a previous write is still in flight
    // (the caller retries next frame) or the write failed (see LastWriteError).
    bool Send(const PadOutput& o) {
        if (!IsOpen()) return false;
        if (writePending_) {
            DWORD n = 0;
            if (!GetOverlappedResult(handle_, &wov_, &n, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) return false;
                writeError_ = GetLastError();
            }
            writePending_ = false;
        }
        // Windows HID writes must be as long as the device's largest output report (hidapi pads the same way):
        // Bluetooth reports 547 here although report 0x31 itself is 78 bytes; the CRC covers only those 78.
        BYTE r[1024] = {};
        BYTE* c;
        DWORD len;
        if (bluetooth_) {
            len = outLen_ > 78 ? outLen_ : 78;
            r[0] = 0x31; r[1] = (BYTE)(seq_ << 4); r[2] = 0x10;
            seq_ = (seq_ + 1) & 0x0F;
            c = r + 3;
        } else {
            len = outLen_ ? outLen_ : 63;
            r[0] = 0x02;
            c = r + 1;
        }
        if (len > sizeof(r)) len = sizeof(r);
        c[0] = 0x01 | 0x02 | 0x04 | 0x08;          // flag0: compatible vibration, haptics select, right + left trigger effect
        c[1] = 0x04 | 0x10;                        // flag1: lightbar, player indicator
        c[2] = o.motorRight;
        c[3] = o.motorLeft;
        memcpy(c + 10, o.rightTrigger, 11);
        memcpy(c + 21, o.leftTrigger, 11);
        c[38] = 0x04;                              // flag2: vibration v2 (newer firmware)
        if (!lightbarReleased_) { c[38] |= 0x02; c[41] = 0x02; lightbarReleased_ = true; }   // end the pairing light animation
        c[42] = 0x00;                              // LED brightness: high
        c[43] = o.playerLeds;
        c[44] = o.red; c[45] = o.green; c[46] = o.blue;
        if (bluetooth_) {
            uint32_t crc = Crc32Seeded(r, 74);
            r[74] = (BYTE)crc; r[75] = (BYTE)(crc >> 8); r[76] = (BYTE)(crc >> 16); r[77] = (BYTE)(crc >> 24);
        }
        ResetEvent(wov_.hEvent);
        DWORD n = 0;
        if (WriteFile(handle_, r, len, &n, &wov_)) { writeError_ = 0; return true; }
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) { writePending_ = true; writeError_ = 0; return true; }
        writeError_ = e;
        return false;
    }

    void Parse(const BYTE* r, int len, PadState& st) const {
        if (len < 10) return;
        const BYTE* d = nullptr;
        if (!bluetooth_ && r[0] == 0x01) d = r + 1;                   // USB full report
        else if (bluetooth_ && r[0] == 0x31) d = r + 2;               // Bluetooth full report
        st.reportId = r[0]; st.reportLen = len; st.bluetooth = bluetooth_;
        if (d) {
            st.lx = d[0]; st.ly = d[1]; st.rx = d[2]; st.ry = d[3]; st.l2 = d[4]; st.r2 = d[5];
            SetButtons(st, d[7], d[8], d[9]);
        } else if (bluetooth_ && r[0] == 0x01) {                      // Bluetooth simple report
            st.lx = r[1]; st.ly = r[2]; st.rx = r[3]; st.ry = r[4];
            SetButtons(st, r[5], r[6], r[7]);
            st.l2 = r[8]; st.r2 = r[9];
        } else {
            return;
        }
        st.connected = true;
    }

private:
    // CRC-32 (zlib polynomial) over the seed byte 0xA2 followed by the report, as hid-playstation does.
    static uint32_t Crc32Seeded(const BYTE* data, int len) {
        uint32_t crc = 0xFFFFFFFF;
        auto step = [&](BYTE b) {
            crc ^= b;
            for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        };
        step(0xA2);
        for (int i = 0; i < len; ++i) step(data[i]);
        return ~crc;
    }

    // Reading feature report 0x05 (calibration) makes a Bluetooth DualSense send full 0x31 reports.
    // The handle is overlapped, so issue the IOCTL with an OVERLAPPED structure (as hidapi does).
    void RequestFullReports() {
        featResult_ = 0; featError_ = 0;
        BYTE feat[256] = { 0x05 };
        DWORD len = featLen_ ? featLen_ : 64;
        if (len > sizeof(feat)) len = sizeof(feat);
        OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD got = 0;
        BOOL ok = DeviceIoControl(handle_, IOCTL_HID_GET_FEATURE, feat, len, feat, len, &got, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = WaitForSingleObject(ov.hEvent, 1000) == WAIT_OBJECT_0 && GetOverlappedResult(handle_, &ov, &got, FALSE);
        if (!ok) { featError_ = GetLastError(); CancelIo(handle_); }
        CloseHandle(ov.hEvent);
        featResult_ = ok ? 1 : 0;
    }

    static void SetButtons(PadState& st, BYTE b0, BYTE b1, BYTE b2) {
        st.hat = b0 & 0x0F;
        st.square = b0 & 0x10; st.cross = b0 & 0x20; st.circle = b0 & 0x40; st.triangle = b0 & 0x80;
        st.l1 = b1 & 0x01; st.r1 = b1 & 0x02; st.l2b = b1 & 0x04; st.r2b = b1 & 0x08;
        st.create = b1 & 0x10; st.options = b1 & 0x20; st.l3 = b1 & 0x40; st.r3 = b1 & 0x80;
        st.ps = b2 & 0x01; st.touchpad = b2 & 0x02; st.mute = b2 & 0x04;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED ov_{};
    OVERLAPPED wov_{};
    bool pending_ = false;
    bool writePending_ = false;
    bool bluetooth_ = false;
    bool lightbarReleased_ = false;
    int featResult_ = -1;
    DWORD featError_ = 0;
    DWORD writeError_ = 0;
    BYTE buf_[128]{};
    BYTE seq_ = 0;
    USHORT inLen_ = 0, featLen_ = 0, outLen_ = 0;
    std::wstring path_;
};
