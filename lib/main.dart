import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'services/ble_service.dart';
import 'services/storage_service.dart';

void main() {
  runApp(const SafetyAlertApp());
}

class SafetyAlertApp extends StatelessWidget {
  const SafetyAlertApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'ESP32 Safety Alert',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.red,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const HomeScreen(),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final BleService _bleService = BleService();
  final StorageService _storageService = StorageService();

  List<ScanResult> _scanResults = [];
  List<String> _contacts = [];
  String _statusMessage = "Ready";
  bool _isScanning = false;
  final TextEditingController _contactController = TextEditingController();
  final TextEditingController _macController = TextEditingController(
    text: "B4:66:85:D6:CD:C0",
  );

  @override
  void initState() {
    super.initState();
    _loadContacts();
    _bleService.onStatusUpdate = (msg) {
      if (mounted) {
        setState(() {
          _statusMessage = msg;
        });
      }
    };
  }

  Future<void> _loadContacts() async {
    final contacts = await _storageService.getContacts();
    if (mounted) {
      setState(() {
        _contacts = contacts;
      });
    }
  }

  void _addContact() async {
    if (_contactController.text.isNotEmpty) {
      await _storageService.addContact(_contactController.text);
      _contactController.clear();
      _loadContacts();
    }
  }

  void _removeContact(String contact) async {
    await _storageService.removeContact(contact);
    _loadContacts();
  }

  void _forceConnect([String? manualMac]) async {
    String targetMac = manualMac ?? _macController.text.trim();
    if (targetMac.isEmpty) return;

    setState(() {
      _statusMessage = "Initiating direct connection...";
    });

    bool success = await _bleService.connectById(targetMac);
    if (!success) {
      if (mounted) {
        setState(() {
          _statusMessage = "Force connect failed. Try standard scan.";
        });
      }
    }
  }

  void _startScan() async {
    setState(() {
      _isScanning = true;
      _scanResults = [];
      _statusMessage = "Starting scan...";
    });

    try {
      _statusMessage = "Checking permissions...";
      await Permission.location.request();
      await Permission.bluetoothScan.request();
      await Permission.bluetoothConnect.request();

      if (await Permission.location.isDenied) {
        setState(() {
          _statusMessage = "LOCATION PERMISSION DENIED.";
          _isScanning = false;
        });
        return;
      }

      BluetoothAdapterState state = await FlutterBluePlus.adapterState.first;
      if (state != BluetoothAdapterState.on) {
        setState(() {
          _statusMessage = "BLUETOOTH IS OFF.";
          _isScanning = false;
        });
        return;
      }

      bool isLocationEnabled =
          await Permission.location.serviceStatus.isEnabled;
      if (!isLocationEnabled) {
        setState(() {
          _statusMessage = "LOCATION (GPS) IS OFF.";
          _isScanning = false;
        });
        return;
      }

      _statusMessage = "Scanning for all Bluetooth devices...";
      await _bleService.startScan((result) {
        if (mounted) {
          setState(() {
            int existingIndex = _scanResults.indexWhere(
              (r) => r.device.remoteId == result.device.remoteId,
            );

            if (existingIndex == -1) {
              _scanResults.add(result);
            } else {
              _scanResults[existingIndex] = result;
            }
            _statusMessage = "Found ${_scanResults.length} devices...";
          });
        }
      });

      await Future.delayed(const Duration(seconds: 10));
      _statusMessage = "Scan complete. Connect below.";
    } catch (e) {
      if (mounted) {
        setState(() {
          _statusMessage = "Scan error: $e";
        });
      }
    } finally {
      if (mounted) {
        setState(() {
          _isScanning = false;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("🚨 Safety Alert System"),
        centerTitle: true,
        backgroundColor: Theme.of(context).colorScheme.errorContainer,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Row(
                  children: [
                    Icon(
                      _bleService.connectedDevice != null
                          ? Icons.bluetooth_connected
                          : Icons.bluetooth_disabled,
                      color: _bleService.connectedDevice != null
                          ? Colors.green
                          : Colors.red,
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Text(
                        "Status: $_statusMessage",
                        style: const TextStyle(fontWeight: FontWeight.bold),
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 20),

            const Text(
              "0. Bypass - Force Connect",
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
                color: Colors.orange,
              ),
            ),
            const SizedBox(height: 10),
            Card(
              color: Colors.orange.withOpacity(0.1),
              child: Padding(
                padding: const EdgeInsets.all(12.0),
                child: Column(
                  children: [
                    TextField(
                      controller: _macController,
                      decoration: const InputDecoration(
                        labelText: "MAC Address",
                        hintText: "e.g. B4:66:85:D6:CD:C0",
                        border: OutlineInputBorder(),
                      ),
                    ),
                    const SizedBox(height: 10),
                    ElevatedButton.icon(
                      onPressed: () => _forceConnect(),
                      icon: const Icon(Icons.flash_on),
                      label: const Text("FORCE CONNECT BY MAC"),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.orange,
                        foregroundColor: Colors.black,
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 20),

            const Text(
              "1. All Discovered Devices",
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 10),
            ElevatedButton.icon(
              onPressed: _isScanning ? null : _startScan,
              icon: _isScanning
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.search),
              label: Text(_isScanning ? "Scanning..." : "Scan for All Devices"),
            ),
            const SizedBox(height: 10),
            _scanResults.isEmpty && !_isScanning
                ? const Text("No devices found. Ensure GPS is ON.")
                : ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    itemCount: _scanResults.length,
                    itemBuilder: (context, index) {
                      final result = _scanResults[index];
                      final device = result.device;
                      final adv = result.advertisementData;

                      String remoteId = device.remoteId.toString();
                      String displayName = device.platformName.isNotEmpty
                          ? device.platformName
                          : (adv.advName.isNotEmpty
                                ? adv.advName
                                : "Unknown Device");

                      return Card(
                        margin: const EdgeInsets.symmetric(vertical: 4),
                        child: ListTile(
                          leading: const Icon(Icons.bluetooth),
                          title: Text(
                            displayName,
                            style: const TextStyle(fontWeight: FontWeight.bold),
                          ),
                          subtitle: Text("ID: $remoteId"),
                          trailing: ElevatedButton(
                            onPressed: () => _bleService.connect(device),
                            child: const Text("Connect"),
                          ),
                        ),
                      );
                    },
                  ),
            const SizedBox(height: 20),

            const Text(
              "2. Emergency Contacts",
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _contactController,
                    decoration: const InputDecoration(
                      hintText: "Phone number",
                      border: OutlineInputBorder(),
                    ),
                    keyboardType: TextInputType.phone,
                  ),
                ),
                const SizedBox(width: 10),
                IconButton.filled(
                  onPressed: _addContact,
                  icon: const Icon(Icons.add),
                ),
              ],
            ),
            const SizedBox(height: 10),
            _contacts.isEmpty
                ? const Text("No contacts.")
                : ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    itemCount: _contacts.length,
                    itemBuilder: (context, index) {
                      return ListTile(
                        leading: const Icon(Icons.person),
                        title: Text(_contacts[index]),
                        trailing: IconButton(
                          icon: const Icon(Icons.delete, color: Colors.red),
                          onPressed: () => _removeContact(_contacts[index]),
                        ),
                      );
                    },
                  ),
          ],
        ),
      ),
    );
  }
}
