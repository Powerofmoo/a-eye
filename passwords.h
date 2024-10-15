// Define a structure to hold Wi-Fi credentials
struct WiFiCredential {
  const char *ssid;
  const char *password;
};

// Create an array of WiFiCredential structures
WiFiCredential wifiCredentials[] = {
  { "FRITZ!Box Fon WLAN 7170", "##" },
  { "iPhone Richard", "##" },
};

String openAIapiKey = "sk-proj-##";
String elevenLabsApiKey = "sk_##";