import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'location_service.dart';
import 'sms_service.dart';
import 'storage_service.dart';

class BleService {
  final String serviceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  final String characteristicUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

  BluetoothDevice? connectedDevice;
  BluetoothCharacteristic? targetCharacteristic;
  StreamSubscription? characteristicSubscription;

  final LocationService _locationService = LocationService();
  final SmsService _smsService = SmsService();
  final StorageService _storageService = StorageService();

  Function(String)? onStatusUpdate;

  Future<void> startScan(Function(ScanResult) onScanResult) async {
    // Step 1: Get ALL bonded/system devices without filters
    try {
      // First, get currently connected devices known to the plugin
      List<BluetoothDevice> connectedDevices = FlutterBluePlus.connectedDevices;
      for (var device in connectedDevices) {
        _emitScanResult(device, onScanResult);
      }

      // Second, get system-bonded devices (no UUID filter for maximum discovery)
      // Passing an empty list [] returns all bonded devices regardless of services
      List<BluetoothDevice> systemDevices = await FlutterBluePlus.systemDevices(
        [],
      );
      for (var device in systemDevices) {
        _emitScanResult(device, onScanResult);
      }
    } catch (e) {
      print("Error fetching bonded/connected devices: $e");
    }

    // Step 2: Start active scan for new devices
    // Extended timeout to 10 seconds for better discovery
    FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    FlutterBluePlus.scanResults.listen((results) {
      for (ScanResult r in results) {
        onScanResult(r);
      }
    });
  }

  void _emitScanResult(
    BluetoothDevice device,
    Function(ScanResult) onScanResult,
  ) {
    onScanResult(
      ScanResult(
        device: device,
        advertisementData: AdvertisementData(
          advName: device.platformName,
          txPowerLevel: null,
          connectable: true,
          serviceUuids: [],
          manufacturerData: {},
          serviceData: {},
          appearance: null,
        ),
        rssi: -50,
        timeStamp: DateTime.now(),
      ),
    );
  }

  Future<bool> connect(BluetoothDevice device) async {
    try {
      // Step 1: Stop any active scan before connecting (CRITICAL for Android)
      onStatusUpdate?.call("Stopping scan...");
      await FlutterBluePlus.stopScan();

      onStatusUpdate?.call(
        "Connecting to ${device.platformName.isEmpty ? 'Device' : device.platformName}...",
      );

      // Step 2: Connect with a timeout
      await device.connect(
        timeout: const Duration(seconds: 15),
        autoConnect: false,
      );

      connectedDevice = device;
      onStatusUpdate?.call("Connected! Discovering services...");

      List<BluetoothService> services = await device.discoverServices();
      for (BluetoothService service in services) {
        if (service.uuid.toString() == serviceUuid) {
          for (BluetoothCharacteristic characteristic
              in service.characteristics) {
            if (characteristic.uuid.toString() == characteristicUuid) {
              targetCharacteristic = characteristic;
              await characteristic.setNotifyValue(true);
              characteristicSubscription = characteristic.lastValueStream
                  .listen((value) {
                    if (value.isNotEmpty &&
                        String.fromCharCodes(value) == "1") {
                      _triggerAlert();
                    }
                  });
            }
          }
        }
      }
      return true;
    } catch (e) {
      onStatusUpdate?.call("Connection failed: $e");
      return false;
    }
  }

  Future<bool> connectById(String deviceId) async {
    try {
      onStatusUpdate?.call("Force connecting to $deviceId...");
      BluetoothDevice device = BluetoothDevice.fromId(deviceId);
      return await connect(device);
    } catch (e) {
      onStatusUpdate?.call("Direct connect failed: $e");
      return false;
    }
  }

  Future<void> _triggerAlert() async {
    onStatusUpdate?.call("ALERT TRIGGERED!");
    final position = await _locationService.getCurrentLocation();
    if (position != null) {
      String url = _locationService.formatLocationUrl(position);
      List<String> contacts = await _storageService.getContacts();
      await _smsService.sendAlertToMultiple(contacts, url);
      onStatusUpdate?.call("Alert sent with location.");
    } else {
      onStatusUpdate?.call("Failed to get location for alert.");
    }
  }

  Future<void> disconnect() async {
    await characteristicSubscription?.cancel();
    await connectedDevice?.disconnect();
    connectedDevice = null;
    targetCharacteristic = null;
    onStatusUpdate?.call("Disconnected");
  }
}
