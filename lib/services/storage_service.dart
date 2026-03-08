import 'package:shared_preferences/shared_preferences.dart';

class StorageService {
  static const String _contactsKey = 'emergency_contacts';

  Future<void> saveContacts(List<String> contacts) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(_contactsKey, contacts);
  }

  Future<List<String>> getContacts() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getStringList(_contactsKey) ?? [];
  }

  Future<void> addContact(String contact) async {
    List<String> contacts = await getContacts();
    if (!contacts.contains(contact)) {
      contacts.add(contact);
      await saveContacts(contacts);
    }
  }

  Future<void> removeContact(String contact) async {
    List<String> contacts = await getContacts();
    contacts.remove(contact);
    await saveContacts(contacts);
  }
}
