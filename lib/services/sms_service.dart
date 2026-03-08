import 'package:telephony/telephony.dart';
import 'package:permission_handler/permission_handler.dart';

class SmsService {
  final Telephony telephony = Telephony.instance;

  Future<bool> sendSms(String number, String message) async {
    // Request SMS permission
    var status = await Permission.sms.request();
    if (status.isGranted) {
      try {
        await telephony.sendSms(to: number, message: message);
        return true;
      } catch (e) {
        return false;
      }
    } else {
      return false;
    }
  }

  Future<void> sendAlertToMultiple(
    List<String> numbers,
    String locationUrl,
  ) async {
    String message =
        "EMERGENCY ALERT! I might be in trouble. My live location: $locationUrl";
    for (String number in numbers) {
      await sendSms(number, message);
    }
  }
}
