#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <cstring>
#include "sbc.h"
#include "PCMServer.h"
#include <time.h>
#include "denoise.h"

extern "C" void InitBLEAudio();
extern "C" void onAudioDataReceived(const uint8_t* data, size_t len);
extern "C" void onAIButtonEvent(uint8_t value);


extern std::ofstream pcmFile;
extern bool recording;

static struct timespec pressTime;
static bool aiKeyPressed = false;

PCMServer pcmServer;
static sbc_t sbc_context;
static bool sbc_initialized = false;


// self-defined AI key map
std::map<uint16_t, std::string> aiKeyMap = {
    {0x20, "AI 键"},
    {0xFF09, "AI 键 1 长文写作"},
    {0xFF10, "AI 键 2 智能体"},
    {0xFF11, "AI 键 3 PPT"},
    {0xFF06, "AI 键 4 方案策划"},
    {0xFF07, "AI 键 5 工作总结"},
    {0xFF08, "AI 键 6 演讲稿"},
    {0xFF03, "AI 键 7 文本润色"},
    {0xFF04, "AI 键 8 文稿校对"},
    {0xFF05, "AI 键 9 AI阅读"},
    {0xFF01, "AI 键 / 截图"},
    {0xFF02, "AI 键 * 绘图"},
    {0xFF12, "AI 键 0 图像识别"},
    {0xFF13, "AI 键 . 翻译"},
    {0xFF14, "AI 键 - 录音转写"},
    {0xFF15, "AI 键 + 写作"},
    {0xFF16, "AI 键 Enter AI问答"}
};

// device connect
void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "✅ HID device connected." << std::endl;
}

// device removal
void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "❌ HID device removed." << std::endl;
}

size_t sbc_decode(const uint8_t* input, size_t input_len, uint8_t* output, size_t output_max_len) {
    if (!sbc_initialized) {
        if (sbc_init(&sbc_context, 0) != 0) {
            std::cerr << "Failed to initialize SBC decoder\n";
            return 0;
        }
        sbc_initialized = true;
    }

    size_t written = 0;
    ssize_t decoded = ::sbc_decode(&sbc_context, input, input_len, output, output_max_len, &written);
    if (decoded < 0) {
        std::cerr << "sbc_decode failed\n";
        return 0;
    }

    return written;
}

void HandleInput(void* context, IOReturn result, void* sender, IOHIDValueRef value) {
    IOHIDElementRef element = IOHIDValueGetElement(value);
    uint32_t usagePage = IOHIDElementGetUsagePage(element);
    uint32_t usage = IOHIDElementGetUsage(element);
    CFIndex length = IOHIDValueGetLength(value);
    const uint8_t* data = (const uint8_t*)IOHIDValueGetBytePtr(value);
    
    /*std::cout << "usagePage = 0x" << std::hex << usagePage << ", usage = 0x" << std::hex << usage << std::endl;
    std::cout << "Input data (len=" << std::dec << length << "): ";
    for (CFIndex i = 0; i < length; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::endl;*/
    
    // Handle audio data
    if (usagePage == 0xFF02 && length >= 3 && data[0] == 0x01)
    {
        if (!aiKeyPressed)
        {
            // Press AI key first time
            aiKeyPressed = true;
            clock_gettime(CLOCK_MONOTONIC, &pressTime);
            //std::cout << "🔘 Press AI key" << std::endl;
        }
        else {
            struct timespec currentTime;
            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            double duration = (currentTime.tv_sec - pressTime.tv_sec) + (currentTime.tv_nsec - pressTime.tv_nsec) / 1e9;
            //std::cout << "duration = " << duration << "s" << std::endl;
            if (duration > 0.5)
            {
                if (recording == false)
                {
                    std::cout << "⏱️ Long press AI key, start to receive audio..." << std::endl;
                    recording = true;
                    pcmServer.sendKeyboard(32, 1, 2);  // Send long press AI key event to client
                }
                
                // Handle audio decode
                static sbc_t sbc;
                static bool initialized = false;
                if (!initialized) {
                    sbc_init_msbc(&sbc, 0);
                    sbc.endian = SBC_LE;
                    initialized = true;
                }
                
                const size_t msbc_data_len = 57;
                const uint8_t* msbc_data = data + 2;
                
                int16_t pcm_output[240] = {0};
                size_t pcm_len = 0;
                
                ssize_t result = sbc_decode(&sbc, msbc_data, msbc_data_len, (uint8_t *)pcm_output, sizeof(pcm_output), &pcm_len);
                
                if (result > 0 && pcm_len > 0)
                {
                    if (!pcmFile.is_open())
                    {
                        pcmFile.open("audio_data_decoded.pcm", std::ios::binary | std::ios::trunc);
                        if (!pcmFile)
                        {
                            std::cerr << "❌ Can't open PCM file to write\n";
                            return;
                        }
                    }
                    
                    pcmFile.write(reinterpret_cast<const char*>(pcm_output), pcm_len);
                    pcmFile.flush();
                    // Can run "ffmpeg -f s16le -ar 16000 -ac 1 -i audio_data_decoded.pcm output.wav" to convert from pcm to wav
                    //std::cout << "✅ Write PCM: " << pcm_len << " bytes\n";
                    // Send audio data to client
                    //pcmServer.sendAudioPCM((uint8_t*)pcm_output, pcm_len);
                }
                else
                {
                    std::cerr << "❌ mSBC decode failed, error code: " << result << std::endl;
                }
            }
        }
    }
    else if (usagePage == 0x0c && length == 1 && data[0] == 0x00)
    {
        // Release AI key
        if (aiKeyPressed)
        {
            struct timespec releaseTime;
            clock_gettime(CLOCK_MONOTONIC, &releaseTime);
            double duration = (releaseTime.tv_sec - pressTime.tv_sec) + (releaseTime.tv_nsec - pressTime.tv_nsec) / 1e9;
            //std::cout << "duration = " << duration << "s" << std::endl;
            if (duration >= 1)
            {
                std::cout << "🎤 audio data ends" << std::endl;
                recording = false;
                pcmServer.sendKeyboard(32, 0, 2);  // Send release AI key event to client
            }
            else
            {
                std::cout << "🖱️ Click AI key" << std::endl;
            }
            aiKeyPressed = false;
        }
    }
    // Handle keyboard press
    else if (usagePage == 0x0C && length == 2 && usage == 0xffffffff){
        uint16_t keyCode = data[1] << 8 | data[0];  // 小端
        auto it = aiKeyMap.find(keyCode);
        if (it != aiKeyMap.end())
        {
            std::cout << "Detect " << it->second << std::endl;
            //pcmServer.sendKeyboard(keyCode, 1, 0);
        }
        else if (keyCode == 0x0)
        {
            std::cout << "Release " << std::endl;
        }
        /*else
        {
            std::cout << "Unknown keyboard: 0x" << std::hex << keyCode << std::endl;
        }*/
    }
}


int main() {
    // === start TCP server ===
    /*if (!pcmServer.start()) {
        std::cerr << "Failed to start PCM TCP server.\n";
        return -1;
    }
    std::cout << "Start TCP server " << std::endl;*/
    
    // === initialize HID Manager ===
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create IOHIDManager\n";
        return -1;
    }

    CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    int vendorID = 0x248A;
    //int productID = 0xCA10; // 2.4G PID
    int productID = 0x8266;  // bluetooth PID
    //int productID = 0x8271;  // MiShu bluetoot PID
    CFNumberRef vendorIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
    CFNumberRef productIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID);

    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendorIDRef);
    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDProductIDKey), productIDRef);

    CFRelease(vendorIDRef);
    CFRelease(productIDRef);

    IOHIDManagerSetDeviceMatching(hidManager, matchingDict);
    // register device connect/remove callbacks
    IOHIDManagerRegisterDeviceMatchingCallback(hidManager, DeviceConnectedCallback, nullptr);
    IOHIDManagerRegisterDeviceRemovalCallback(hidManager, DeviceRemovedCallback, nullptr);
    CFRelease(matchingDict);

    IOHIDManagerRegisterInputValueCallback(hidManager, HandleInput, nullptr);
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open HID Manager\n";
        return -1;
    }
    
    //  print Transport type: USB or BLE
    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    if (deviceSet) {
        CFIndex count = CFSetGetCount(deviceSet);
        IOHIDDeviceRef* devices = (IOHIDDeviceRef*)malloc(sizeof(IOHIDDeviceRef) * count);
        CFSetGetValues(deviceSet, (const void**)devices);
        for (CFIndex i = 0; i < count; ++i) {
            CFTypeRef transport = IOHIDDeviceGetProperty(devices[i], CFSTR("Transport"));
            if (transport && CFGetTypeID(transport) == CFStringGetTypeID()) {
                char buffer[256];
                CFStringGetCString((CFStringRef)transport, buffer, sizeof(buffer), kCFStringEncodingUTF8);
                std::cout << "Detected device transport: " << buffer << std::endl;
            }
        }
        free(devices);
        CFRelease(deviceSet);
    }

    std::cout << "Listening for HID input and BLE audio...\n";
    
    // === initialize BLE ===
    InitBLEAudio();  // 👈 调用 BLE 初始化
    CFRunLoopRun();

    // ==== Terminate and cleanup ===
    /*if (pcmFile.is_open()) {
        pcmFile.close();
    }
    
    pcmServer.stop(); // stop TCP server*/

    CFRelease(hidManager);

    return 0;
}
