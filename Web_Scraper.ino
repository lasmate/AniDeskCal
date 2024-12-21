#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

#include <MD_MAX72xx.h>
#include <EEPROM.h>
#include <SPI.h>

#include <curl/curl.h>
#include <string>
#include <iostream>
#include <regex>

#define TEST_HOST "Anilist.com"
#define MAX_COORDINATES 624

// Define hardware type, size, and pin connections
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4  // Number of displays connected in series

#define EEPROM_SIZE 512         // Define the size of EEPROM
#define ANILIST_USERNAME_ADDR 0  // Start address for Anilist username

#define DATA_PIN   D7      // GPIO 13 on ESP8266 (D7 on NodeMCU)
#define CS_PIN     D8      // GPIO 15 on ESP8266 (D8 on NodeMCU)
#define CLK_PIN    D5      // GPIO 14 on ESP8266 (D5 on NodeMCU)
#define RESET_PIN  D6      // GPIO 12 on ESP8266 (D6 on NodeMCU)

#define WIFI "Who stole my mom?"
#define PASSWORD "goose"

MD_MAX72XX display = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Function prototype
void makeHTTPRequest(int* count, int coordinates[MAX_COORDINATES][2]);

WiFiManager wm;                               // Global WiFiManager instance
WiFiManagerParameter custom_Anilist_username;  // Parameter for Anilist username

WiFiClientSecure client;
int coordinates[MAX_COORDINATES][2];
int count = 0;

// Button press duration
unsigned long buttonPressStartTime = 0;
bool isButtonPressed = false;

void setup() {
  Serial.begin(230400);

  // Initialize button pin
  pinMode(RESET_PIN, INPUT_PULLUP);

  display.begin();  // Initialize the display
  display.clear();  // Clear any previous data

  EEPROM.begin(EEPROM_SIZE);  // Initialize EEPROM
  String storedUsername = readAnilistUsername();
  String storedWIFI = WIFI;

  // Button press duration
  unsigned long buttonPressStartTime = 0;
  bool isButtonPressed = false;

  // Check if a Anilist username is stored in EEPROM
  if (storedUsername.length() > 0) {
    Serial.println("Stored Anilist Username: " + storedUsername);

    // Display the stored username on the LED matrix
    displayTextOnMatrix("Hello, " + storedUsername);
  } else {
    Serial.println("No Anilist Username found in EEPROM.");
  }

  // Define the custom parameter for the Anilist username
  int customFieldLength = 40;  // Maximum length for the Anilist username input
  new (&custom_Anilist_username) WiFiManagerParameter("Anilist_username", "Anilist Username", "", customFieldLength, "required placeholder=\"Enter Anilist Username\"");

  // Add the custom parameter to WiFiManager
  wm.addParameter(&custom_Anilist_username);
  wm.setSaveParamsCallback(saveParamCallback);  // Callback to save custom parameters

  Serial.print("Connecting to WiFi using WiFiManager");

  if (!wm.autoConnect(WIFI, PASSWORD)) {
    Serial.println("Failed to connect or hit timeout");
    displayTextOnMatrix("Connect to: " + storedWIFI);
    delay(3000);
    ESP.restart();  // Restart if it fails to connect
  } else {
    Serial.println("Connected to WiFi!");

    // Ensure the Anilist username is provided
    String AnilistUsername = readAnilistUsername();
    Serial.println(AnilistUsername);
    if (strlen(AnilistUsername.c_str()) == 0) {
      Serial.println("Anilist Username is required! Please try again.");
      wm.resetSettings();
      ESP.restart();  // Restart to reinitialize WiFiManager
    } else {
      Serial.println("Anilist Username: " + AnilistUsername);
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());

      saveAnilistUsername(AnilistUsername.c_str());  // Save username to EEPROM
      client.setInsecure();
      makeHTTPRequest(&count, coordinates);
    }
  }
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void makeHTTPRequest() {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://graphql.anilist.co");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "{\"query\":\"query { MediaListCollection(userName: \\\"YOUR_ANILIST_USERNAME\\\", type: ANIME) { lists { entries { status } } } }\"}");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Set insecure connection

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            std::cout << "Received response from AniList API" << std::endl;
            std::cout << readBuffer << std::endl;

            // Extract data from the div with class "activity-history"
            std::regex div_regex("<div class=\"activity-history\">(.*?)</div>");
            std::smatch match;
            if (std::regex_search(readBuffer, match, div_regex)) {
                std::cout << "Activity History: " << match[1] << std::endl;
            } else {
                std::cout << "No activity history found." << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

// Helper function to extract attribute value
String extractAttribute(String element, String attribute) {
  String attributeValue = "";
  int attrStartIndex = element.indexOf(attribute);
  if (attrStartIndex != -1) {
    int valueStartIndex = attrStartIndex + attribute.length();
    int valueEndIndex = element.indexOf("\"", valueStartIndex);
    if (valueEndIndex != -1) {
      attributeValue = element.substring(valueStartIndex, valueEndIndex);
    }
  }
  return attributeValue;
}

// Save parameter callback
void saveParamCallback() {
  Serial.println("[CALLBACK] saveParamCallback fired");
  String AnilistUsername = String(custom_Anilist_username.getValue());
  Serial.println("Anilist Username = " + AnilistUsername);
  saveAnilistUsername(AnilistUsername.c_str());
}


void saveAnilistUsername(const char* username) {
  int i = 0;
  for (; i < strlen(username); i++) {
    EEPROM.write(ANILIST_USERNAME_ADDR + i, username[i]);  // Write username to EEPROM
  }
  EEPROM.write(ANILIST_USERNAME_ADDR + i, '\0');  // Null-terminate the string
  EEPROM.commit();                               // Save changes
  Serial.println("Anilist username saved to EEPROM.");
}

String readAnilistUsername() {
  char username[40];  // Adjust size as needed
  int i = 0;
  while (i < 40) {  // Reading username from EEPROM
    char c = EEPROM.read(ANILIST_USERNAME_ADDR + i);
    if (c == '\0') break;
    username[i] = c;
    i++;
  }
  username[i] = '\0';  // Null-terminate the string
  return String(username);
}

// Function to display text on the LED matrix across 4 displays
void displayTextOnMatrix(String text) {
  display.clear();  // Clear the display before showing the text
  int textLength = text.length();
  int maxDisplayChars = MAX_DEVICES * 8;  // Each display can show 8 columns

  // Scroll the text if it exceeds the available width of 4 displays
  for (int i = 0; i < textLength * 8 + maxDisplayChars; i++) {
    display.clear();  // Clear the display for each scroll step

    // Display the text with scrolling effect
    for (int j = 0; j < textLength; j++) {
      display.setChar((j * 8) - i, text[j]);  // Shift each character based on scroll position
    }

    display.update();  // Update the display to reflect changes
    delay(100);        // Adjust scrolling speed (lower for faster, higher for slower)
  }
}

// Function to reset WiFi credentials and Anilist username
void resetSettings() {
  Serial.println("Resetting settings...");

  // Reset WiFi settings
  wm.resetSettings();

  // Clear Anilist username from EEPROM
  for (int i = 0; i < 40; i++) {
    EEPROM.write(ANILIST_USERNAME_ADDR + i, 0);
  }
  EEPROM.commit();

  // Restart the device
  ESP.restart();
}

void loop() {
  int refetchCounter = 0;
  // Check button press
  if (digitalRead(RESET_PIN) == LOW) { // Button pressed (assuming active low)
    if (!isButtonPressed) {
      buttonPressStartTime = millis();
      isButtonPressed = true;
    }
    // Check if button is held for more than 5 seconds
    if (isButtonPressed && (millis() - buttonPressStartTime > 5000)) {
      resetSettings();
    }
  } else {
    isButtonPressed = false;
  }

  refetchCounter++;

  // Refetch Data after 60 secs
  if (refetchCounter == 60){
    Serial.println("Refetching Data");
    refetchCounter = 0;
    makeHTTPRequest(&count, coordinates);
  }
  delay(1000); // Adjust the loop interval
}
