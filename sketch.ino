#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

// API Endpoints
const char* loginEndpoint = "https://meditracker-backend.onrender.com/api/users/login";
const char* schedulesEndpoint = "https://meditracker-backend.onrender.com/api/schedules";

// Web server on ESP32
WebServer server(80);

// GPIO Pins for LEDs and buzzer
const int ledPins[] = { 2, 4, 5, 18 };
const int buzzerPin = 19;
const int buttonPin = 21;

bool isBuzzerActive = false;
bool remindersActive[4] = {false, false, false, false};
unsigned long reminderStartTimes[4] = {0, 0, 0, 0};
unsigned long blinkIntervals[4] = {0, 0, 0, 0};

// HTML form for login
String loginPage = R"(
  <!DOCTYPE html>
  <html>
  <body>
    <h2>Login</h2>
    <form action="/login" method="POST">
      Email:<br>
      <input type="text" name="email" value=""><br>
      Password:<br>
      <input type="password" name="password" value=""><br><br>
      <input type="submit" value="Login">
    </form>
  </body>
  </html>
)";

String token = "";  // Store token in memory


void setup() {
  Serial.begin(115200);

  // Set pins for LEDs and buzzer
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
  }
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  connectToWiFi();

  // Print the IP address of the ESP32
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Start the web server immediately
  startWebServer();
}


void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
}

void startWebServer() {
  server.on("/", []() {
    server.send(200, "text/html", loginPage);
  });

  server.on("/login", HTTP_POST, []() {
    if (server.hasArg("email") && server.hasArg("password")) {
      String email = server.arg("email");
      String password = server.arg("password");

      if (performLogin(email, password)) {
        server.send(200, "text/plain", "Login successful! Token stored in memory.");
      } else {
        server.send(401, "text/plain", "Login failed. Please try again.");
      }
    } else {
      server.send(400, "text/plain", "Email and password are required.");
    }
  });

  server.begin();
  Serial.println("Web server started.");
}

bool performLogin(String email, String password) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(loginEndpoint);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    String payload = "{\"email\":\"" + email + "\", \"password\":\"" + password + "\"}";
    int httpCode = http.POST(payload);

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Login successful!");
        Serial.println(response);

        // Parse the JSON data
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, response);

        // Get the token from the response
        token = doc["token"].as<String>();
        Serial.println("Token: " + token);

        return true;  // Indicate successful login
      } else {
        Serial.println("Login failed: " + String(httpCode));
      }
    } else {
      Serial.println("Failed to connect to server");
    }

    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
  return false;  // Indicate failed login
}

void fetchSchedule() {
  if (WiFi.status() == WL_CONNECTED && token.length() > 0) {
    HTTPClient http;
    http.begin(schedulesEndpoint);
    http.addHeader("Authorization", String("Bearer ") + token);

    int httpCode = http.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Schedule fetched successfully!");
        Serial.println(payload);

        DynamicJsonDocument doc(2048);
        deserializeJson(doc, payload);

        JsonObject compartments = doc["compartments"];
        handleCompartment(compartments["compartment1"], 0);
        handleCompartment(compartments["compartment2"], 1);
        handleCompartment(compartments["compartment3"], 2);
        handleCompartment(compartments["compartment4"], 3);
      } else {
        Serial.println("Error fetching schedule: " + String(httpCode));
      }
    } else {
      Serial.println("Failed to connect to server");
    }

    http.end();
  } else {
    Serial.println("WiFi not connected or token missing");
  }
}

// Handle compartment's medicine schedule
void handleCompartment(JsonArray compartment, int compartmentIndex) {
  if (compartment.size() > 0) {
    for (JsonVariant medicine : compartment) {
      String medicineName = medicine["name"];
      String time = medicine["time"];
      bool status = medicine["status"];

      Serial.print("Compartment ");
      Serial.print(compartmentIndex + 1);
      Serial.println(" Medicine: " + medicineName + " Time: " + time);

      if (status) {
        startReminder(compartmentIndex);  // Activate reminder for this compartment
      }
    }
  }
}

void loop() {
  server.handleClient();  // Handle client requests for login
  unsigned long currentMillis = millis();

  // Handle reminders for each compartment
  for (int i = 0; i < 4; i++) {
    if (remindersActive[i]) {
      // Check if 10 minutes have passed for the reminder
      if (currentMillis - reminderStartTimes[i] < 10 * 60 * 1000) {
        // Blink LED every 500ms
        if (currentMillis - blinkIntervals[i] >= 500) {
          blinkIntervals[i] = currentMillis;
          int ledState = digitalRead(ledPins[i]);
          digitalWrite(ledPins[i], !ledState);  // Toggle LED
        }
      } else {
        remindersActive[i] = false;  // Stop the reminder after 10 minutes
        digitalWrite(ledPins[i], LOW);  // Ensure LED is off
      }
    }
  }

  // Handle buzzer activation (active if any reminder is active)
  if (isBuzzerActive) {
    tone(buzzerPin, 1000);  // Activate buzzer
  } else {
    noTone(buzzerPin);  // Turn off the buzzer
  }

  // Check if button is pressed to stop all reminders
  if (digitalRead(buttonPin) == LOW) {
    stopAllReminders();
  }
}

void stopAllReminders() {
  isBuzzerActive = false;
  for (int i = 0; i < 4; i++) {
    remindersActive[i] = false;
    digitalWrite(ledPins[i], LOW);  // Ensure all LEDs are off
  }
}

void startReminder(int compartmentIndex) {
  remindersActive[compartmentIndex] = true;
  reminderStartTimes[compartmentIndex] = millis();
  blinkIntervals[compartmentIndex] = millis();
  isBuzzerActive = true;  // Activate buzzer as soon as any reminder starts
}
