// Smart Access Locker - NodeMCU ESP8266 Wi-Fi part (based on Lab 8)
//
// The STM32 sends one text line per event on its USART1 (PB6) to the RX pin
// of this board. This sketch:
//   1) publishes every line to an MQTT topic (Lab 8)
//   2) sends a phone notification through ntfy for pass / fail / alert events
//      (ntfy mobile app, subscribed to the topic "coe411-project")
//
// Wiring:  STM32 PB6 (USART1 TX) -> NodeMCU RX
//          STM32 GND             -> NodeMCU GND
//
// IMPORTANT: unplug the wire on the RX pin while uploading this sketch,
// because the USB upload uses the same RX pin.

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>   // for the ntfy notifications
#include <PubSubClient.h>

const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char *MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const char *CLIENT_ID = "coe411-locker-7k3q9-esp8266";
const char *EVENT_TOPIC = "coe411/f26/locker-7k3q9/events";

const char *NTFY_URL = "http://ntfy.sh/coe411-project";   // our ntfy topic

WiFiClient networkClient;
PubSubClient mqttClient(networkClient);

unsigned long lastReconnectAttempt = 0;
String line = "";   // text received from the STM32 so far

// Send one notification to the ntfy app.
// title    = bold first line of the notification
// priority = 1 (min) to 5 (urgent); 5 keeps ringing on the phone
// tags     = emoji names shown before the title
void sendNotification(const char *title, const char *priority, const char *tags, String message)
{
    WiFiClient ntfyClient;
    HTTPClient http;

    http.begin(ntfyClient, NTFY_URL);
    http.addHeader("Title", title);
    http.addHeader("Priority", priority);
    http.addHeader("Tags", tags);

    int code = http.POST(message);   // 200 means ntfy accepted it
    Serial.print("ntfy reply: ");
    Serial.println(code);

    http.end();
}

// Choose the notification type from the message sent by the STM32
void notifyPhone(String msg)
{
    if (msg.startsWith("ACCESS GRANTED")) {
        sendNotification("Locker opened", "3", "white_check_mark", msg);    // pass
    } else if (msg.startsWith("ACCESS DENIED")) {
        sendNotification("Access denied", "4", "x", msg);                   // fail
    } else if (msg.startsWith("TAMPER")) {
        sendNotification("TAMPER ALERT", "5", "rotating_light", msg);       // alert
    }
    // other messages (LOCKER READY, DOOR LOCKED) only go to MQTT
}

void setup()
{
    Serial.begin(9600);   // must match STM32 USART1 (also set Serial Monitor to 9600)
    delay(1000);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.println("Waiting for Wi-Fi");
        delay(500);
    }

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setSocketTimeout(2);
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED) {
        delay(10);
        return;
    }

    // connect to the MQTT broker again if the connection was lost
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= 5000) {
            lastReconnectAttempt = now;
            if (mqttClient.connect(CLIENT_ID)) {
                Serial.println("MQTT connected");
            } else {
                Serial.print("MQTT state: ");
                Serial.println(mqttClient.state());
            }
        }
        delay(10);
        return;
    }

    mqttClient.loop();

    // read the characters sent by the STM32
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            // end of one message: publish it
            line.trim();   // remove the '\r' at the end
            if (line.length() > 0) {
                if (mqttClient.publish(EVENT_TOPIC, line.c_str(), false)) {
                    Serial.print("Published: ");
                    Serial.println(line);
                } else {
                    Serial.println("Publish failed");
                }
                notifyPhone(line);   // phone notification for pass / fail / alert
            }
            line = "";
        } else if (line.length() < 60) {
            line += c;   // add the character to the message
        }
    }

    delay(10);
}
