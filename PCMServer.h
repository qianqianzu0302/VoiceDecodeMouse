//
//  PCMServer.h
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/6/27.
//

#pragma once
#include <thread>
#include <atomic>
#include <netinet/in.h>

class PCMServer {
public:
    PCMServer(int port = 3395);
    ~PCMServer();

    bool start();
    void stop();
    void sendAudioPCM(uint8_t* data, size_t length);
    void sendKeyboard(uint16_t key, uint8_t state, uint16_t action_type);

private:
    void run();
    int server_fd;
    int client_fd;
    int port;
    std::thread serverThread;
    std::atomic<bool> running;
};
