#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>
#include <iostream>
#include <fstream>

std::ofstream pcmFile;
bool recording = false;

extern "C" void InitBLEAudio();  // 初始化入口

@interface BLEManager : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, assign) BOOL hasSentStartCommand;
@end

@implementation BLEManager {
    CBCentralManager *_central;
    CBPeripheral *_targetPeripheral;
    CBCharacteristic *_audioChar;
    CBCharacteristic *_buttonChar;
}

+ (instancetype)shared {
    static BLEManager *inst;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        inst = [[BLEManager alloc] init];
    });
    return inst;
}

- (instancetype)init {
    if (self = [super init]) {
        _central = [[CBCentralManager alloc] initWithDelegate:self queue:nil];
    }
    return self;
}

#pragma mark - 事件回调

extern "C" void onAIButtonEvent(uint8_t value) {
    std::cout << "[BLE] onAIButtonEvent = " << (int)value << std::endl;
    if (value == 1 && !recording) {
        pcmFile.open("record.pcm", std::ios::binary);
        recording = pcmFile.is_open();
        std::cout << "[BLE] 🎙️ Start Recording = " << recording << std::endl;
    } else if (value == 0 && recording) {
        pcmFile.close();
        recording = false;
        std::cout << "[BLE] 🛑 Stop Recording" << std::endl;
    }
}

extern "C" void onAudioDataReceived(const uint8_t* data, size_t len) {
    if (recording && pcmFile.is_open()) {
        pcmFile.write(reinterpret_cast<const char*>(data), len);
    }
}

#pragma mark - CoreBluetooth 回调

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    if (central.state == CBManagerStatePoweredOn) {
        NSLog(@"[BLE] 🔍 Scanning...");
        [_central scanForPeripheralsWithServices:nil options:nil];
    } else {
        NSLog(@"[BLE] ❌ Bluetooth not available (state = %ld)", (long)central.state);
    }
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
    advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                 RSSI:(NSNumber *)RSSI
{
    NSString *targetName = @"AI-M01"; // 你要匹配的设备名
    
    if (peripheral.name != nil && [peripheral.name containsString:targetName]) {
        NSLog(@"[BLE] 🎯 Found Target Device by Name: %@", peripheral.name);
        
        _targetPeripheral = peripheral;
        _targetPeripheral.delegate = self;
        
        [_central stopScan];
        [_central connectPeripheral:_targetPeripheral options:nil];
    } /*else {
        NSLog(@"[BLE] Discovered device: %@ (ignored)", peripheral.name);
    }*/
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    NSLog(@"[BLE] ✅ Connected: %@", peripheral.identifier.UUIDString);
    self.hasSentStartCommand = NO;
    [peripheral discoverServices:nil];
}

/*- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    if (error) {
        NSLog(@"[BLE] ❌ Discover Services Error: %@", error.localizedDescription);
        return;
    }

    for (CBService *service in peripheral.services) {
        NSLog(@"[BLE] 🧩 Found Service %@", service.UUID.UUIDString);
        if ([service.UUID.UUIDString containsString:@"FF04"]) {
                NSLog(@"[BLE] 🧩 Found FF04 Service");
                [peripheral discoverCharacteristics:nil forService:service];
        }
    }
}*/

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    if (error) {
        NSLog(@"[BLE] ❌ Discover Services Error: %@", error.localizedDescription);
        return;
    }

    for (CBService *service in peripheral.services) {
        NSLog(@"[BLE] 🧩 Found Service %@", service.UUID.UUIDString);
        [peripheral discoverCharacteristics:nil forService:service];
    }
}



/*- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error {

    if (error) {
        NSLog(@"[BLE] ❌ Discover Characteristics Error: %@", error.localizedDescription);
        return;
    }

    for (CBCharacteristic *ch in service.characteristics) {
        NSString *uuid = ch.UUID.UUIDString;
        NSLog(@"[BLE] Characteristic: %@", uuid);

        if (ch.properties & CBCharacteristicPropertyNotify) {
                [peripheral setNotifyValue:YES forCharacteristic:ch];
                NSLog(@"[BLE] Subscribed to notify characteristic: %@", uuid);
        }
    }
}*/
- (void)startListeningForAudioTrigger:(CBPeripheral *)peripheral withCharacteristic:(CBCharacteristic *)ch {
    if ([ch.UUID.UUIDString isEqualToString:@"FF01"]) {
        if (ch.properties & CBCharacteristicPropertyNotify || ch.properties & CBCharacteristicPropertyIndicate) {
            [peripheral setNotifyValue:YES forCharacteristic:ch];
            NSLog(@"[BLE] ✅ Subscribed to FF01 for trigger notifications");
        }
    }
}

- (void)startListeningForAudioData:(CBPeripheral *)peripheral withCharacteristic:(CBCharacteristic *)ch {
    if ([ch.UUID.UUIDString isEqualToString:@"FF05"]) {
        if (ch.properties & CBCharacteristicPropertyNotify || ch.properties & CBCharacteristicPropertyIndicate) {
            [peripheral setNotifyValue:YES forCharacteristic:ch];
            NSLog(@"[BLE] ✅ Subscribed to FF05 for audio data");
        }
    }
}

- (void)tryStartAudio:(CBPeripheral *)peripheral withCharacteristic:(CBCharacteristic *)ch {
    if ([ch.UUID.UUIDString isEqualToString:@"FF02"]) {
        uint8_t startCommand[] = {
            0x03, // cmd = HCI_CMD_VOICE_CONTROL
            0x01, // len = 1
            0x01  // payload = 1
        };
        NSData *commandData = [NSData dataWithBytes:startCommand length:sizeof(startCommand)];
        [peripheral writeValue:commandData forCharacteristic:ch type:CBCharacteristicWriteWithResponse];
        NSLog(@"[BLE] 🚀 Sent structured start command to FF02");
        self.hasSentStartCommand = YES;
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error {

    if (error) {
        NSLog(@"[BLE] ❌ Discover Characteristics Error: %@", error.localizedDescription);
        return;
    }

    for (CBCharacteristic *ch in service.characteristics) {
        NSString *uuid = ch.UUID.UUIDString;
        CBCharacteristicProperties props = ch.properties;

        NSLog(@"[BLE] 🔍 Found characteristic: %@, properties: 0x%02X", uuid, props);

        // 特定处理逻辑
        if ([uuid isEqualToString:@"FF01"]) {
            [self startListeningForAudioTrigger:peripheral withCharacteristic:ch];
        } else if ([uuid isEqualToString:@"FF02"]) {
            NSLog(@"[BLE] ✅ FF02 characteristic discovered, ready to send command later");
        } else if ([uuid isEqualToString:@"FF05"]) {
            [self startListeningForAudioData:peripheral withCharacteristic:ch];
        }

        // 通用监听逻辑：订阅所有支持 Notify 或 Indicate 的特征
        if (props & CBCharacteristicPropertyNotify || props & CBCharacteristicPropertyIndicate) {
            [peripheral setNotifyValue:YES forCharacteristic:ch];
            NSLog(@"[BLE] ✅ Subscribed to %@ for notifications", uuid);
        }
    }
}


- (void)peripheral:(CBPeripheral *)peripheral
didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {

    if (error) {
        NSLog(@"[BLE] ❌ Error updating value for %@: %@", characteristic.UUID.UUIDString, error.localizedDescription);
        return;
    }

    NSString *uuid = characteristic.UUID.UUIDString;
    NSData *data = characteristic.value;

    if ([uuid isEqualToString:@"FF01"]) {
        NSLog(@"[BLE] 🎯 Trigger received from FF01");

        const uint8_t *bytes = (const uint8_t *)data.bytes;
        NSMutableString *hexStr = [NSMutableString string];
        for (NSUInteger i = 0; i < data.length; i++) {
            [hexStr appendFormat:@"%02X ", bytes[i]];
        }
        NSLog(@"[BLE] 📦 FF01 Data: %@", hexStr);

        if (!self.hasSentStartCommand) {
            CBCharacteristic *ff02Char = nil;

            for (CBService *service in peripheral.services) {
                for (CBCharacteristic *ch in service.characteristics) {
                    if ([ch.UUID.UUIDString isEqualToString:@"FF02"]) {
                        ff02Char = ch;
                        break;
                    }
                }
                if (ff02Char) break;
            }

            if (ff02Char) {
                // 延迟 100ms 后写入 FF02
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                    uint8_t startCommand[] = { 0x02, 0x01, 0x01 }; // cmd, len, payload
                    NSData *commandData = [NSData dataWithBytes:startCommand length:sizeof(startCommand)];
                    [peripheral writeValue:commandData forCharacteristic:ff02Char type:CBCharacteristicWriteWithResponse];
                    NSLog(@"[BLE] 🚀 Sent start command to FF02 after 100ms delay");
                    self.hasSentStartCommand = YES;
                });
            } else {
                NSLog(@"[BLE] ⚠️ FF02 characteristic not found");
            }
        } else {
            NSLog(@"[BLE] ⚠️ Start command already sent, skipping.");
        }
    }
    else {
        // 打印所有收到的数据
        NSLog(@"[BLE] 📥 Received data from %@, length = %lu", uuid, (unsigned long)data.length);

        const uint8_t *bytes = (const uint8_t *)data.bytes;
        NSMutableString *hexStr = [NSMutableString string];
        for (NSUInteger i = 0; i < data.length; i++) {
            [hexStr appendFormat:@"%02X ", bytes[i]];
        }
        NSLog(@"[BLE] 🔎 Data from %@: %@", uuid, hexStr);

        // TODO: 根据 uuid 做不同处理，比如 FF05 音频数据
    }
}


- (void)centralManager:(CBCentralManager *)central
didDisconnectPeripheral:(CBPeripheral *)peripheral
                error:(NSError *)error {
    NSLog(@"[BLE] 🔌 Disconnected. Re-scanning...");
    [_central scanForPeripheralsWithServices:nil options:nil];
}

@end

#pragma mark - 初始化入口

extern "C" void InitBLEAudio() {
    NSLog(@"[BLE] 🔧 InitBLEAudio called");
    [BLEManager shared];  // 构造即触发 BLE 流程
}
