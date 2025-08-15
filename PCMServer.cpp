//
//  PCMServer.cpp
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/6/27.
//

#include "PCMServer.h"
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "json.hpp"
#include "base64.h"

using json = nlohmann::json;

PCMServer::PCMServer(int port) : server_fd(-1), client_fd(-1), port(port), running(false) {}

PCMServer::~PCMServer() {
    stop();
}

bool PCMServer::start() {
    running = true;
    serverThread = std::thread(&PCMServer::run, this);
    return true;
}

void PCMServer::stop() {
    running = false;
    if (server_fd != -1) close(server_fd);
    if (client_fd != -1) close(client_fd);
    if (serverThread.joinable()) serverThread.join();
}

void PCMServer::run() {
    struct sockaddr_in address{};
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen failed");
        return;
    }

    std::cout << "Waiting for client to connect on port " << port << "...\n";
    client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (client_fd < 0) {
        perror("accept failed");
        return;
    }

    std::cout << "Client connected!\n";

    while (running) {
        usleep(100000); // idle loop, real data is sent from sendPCM
    }
}

void PCMServer::sendAudioPCM(uint8_t* data, size_t length) {
    if (client_fd == -1 || data == nullptr || length == 0)
        return;

    try {
        // Base64 编码
        std::string base64_data = base64_encode(data, length);

        // 构建 JSON 对象
        json j = {
            {"type", "ON_VOICE_DATA"},
            {"status", "true"},
            {"data", {
                {"length", length},
                {"bytes", base64_data},
                {"bytes_len", base64_data.length()}
            }}
        };

        // 序列化 JSON 并加上分隔符
        std::string response = j.dump() + "|||";

        // 发送数据
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendAudioPCM] Exception: " << e.what() << std::endl;
    }
}

void PCMServer::sendKeyboard(uint16_t key, uint8_t state, uint16_t action_type)
{
    if (client_fd == -1)
        return;
    try {
        json j = {
                {"type", "ON_AI_BUTTON_EVENT"},
                {"status", "true"},
                {"data", {
                    {"key",key},
                    {"state",state},
                    {"action_type",action_type},
                 }}
            };
        
        std::string response = j.dump() + "|||";
        
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendKeyboard] Exception: " << e.what() << std::endl;
    }
}

