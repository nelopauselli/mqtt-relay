#include <Arduino.h>

extern "C" {
#include "user_interface.h"
}

#include "MqttAdapter.h"

#include "Logger.h"
#include "Appenders/SerialAppender.cpp"
#include "TraceMemory.cpp"
#include "Settings.h"
#include "NTPClient.h"
#include "WifiAdapter.h"
#include "TelnetServer.h"
#include "Relay.cpp"
#include "Button.cpp"

#ifdef ARDUINO_ESP8266_NODEMCU
#define RELAY1 D5 //LED_BUILTIN
#define RELAY2 D6
#define BUTTON1 D3
#define BUTTON2 D4
#elif ARDUINO_ESP8266_ESP01
#define RELAY1 1
#define RELAY2 2
#define BUTTON1 0
#define BUTTON2 3
#endif

#include <DHT.h>
DHT dht;

Relay *relay1;
Relay *relay2;
Button *button1;
Button *button2;

TelnetServer *telnetServer = NULL;
MqttAdapter *mqtt = NULL;

void button1_pressed()
{
    mqtt->publish("/home/bathroom/button1/pressed", "");
}

void button2_pressed()
{
    mqtt->publish("/home/bathroom/button2/pressed", "");
}

void relay1_info(const char *message)
{
    char *json = relay1->toJSON();
    mqtt->publish(message, json);
    delete json;
}

void relay1_change()
{
    relay1_info("/home/bathroom/relay1/change");
}

void relay2_info(const char *message)
{
    char *json = relay2->toJSON();
    mqtt->publish(message, json);
    delete json;
}

void relay2_change()
{
    relay2_info("/home/bathroom/relay2/change");
}

void initLogger()
{
    Logger.cleanDebug();
    Logger.cleanLog();
    Logger.debugging(false);

    Logger.add(new SerialAppender());

    delay(10);
}

void initHardware()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Logger.trace("Init relays...");
    delay(10);

    relay1 = new Relay(RELAY1, "extractor");
    relay1->setCallbackOn(relay1_change);
    relay1->setCallbackOff(relay1_change);
    relay2 = new Relay(RELAY2, "toallero");
    relay2->setCallbackOn(relay2_change);
    relay2->setCallbackOff(relay2_change);

    Logger.trace("Init buttons...");
    delay(10);

    button1 = new Button(BUTTON1);
    button1->onPressed(button1_pressed);
    button2 = new Button(BUTTON2);
    button2->onPressed(button2_pressed);

    dht.setup(D2);
}

void initClock()
{
    NTPClient ntpClient;
    if (!ntpClient.initClockFromServer())
        Logger.error("NTP Client init fail. :(");
}

/**
 * 
 * MQTT
 * 
 **/
void device_search()
{
    char *topicBase = Settings.readMqttTopicBase();

    char definition[512];
    strcpy(definition, "{\"topics\":[");

    if (strlen(topicBase) > 0)
    {
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay1/status\",");
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay1/on\",");
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay1/off\",");
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay2/status\",");
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay2/on\",");
        strcat(definition, "\"");
        strcat(definition, topicBase);
        strcat(definition, "/relay2/off\"");
    }
    strcat(definition, "]}");

    mqtt->publish("/devices/found", definition);

    delete topicBase;
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    char *message = new char[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    Serial.println(message);

    if (strcmp(topic, "/devices/search") == 0)
    {
        device_search();
    }
    else if (strcmp(topic, "/home/bathroom/relay1/status") == 0)
    {
        relay1_info(message);
    }
    else if (strcmp(topic, "/home/bathroom/relay1/on") == 0)
    {
        relay1->on();
    }
    else if (strcmp(topic, "/home/bathroom/relay1/off") == 0)
    {
        relay1->off();
    }
    else if (strcmp(topic, "/home/bathroom/relay2/status") == 0)
    {
        relay2_info(message);
    }
    else if (strcmp(topic, "/home/bathroom/relay2/on") == 0)
    {
        relay2->on();
    }
    else if (strcmp(topic, "/home/bathroom/relay2/off") == 0)
    {
        relay2->off();
    }

    delete message;
}

bool reconnect()
{
    String connectionString = Settings.readMqttConnectionString();
    if (connectionString.length() > 0 && connectionString.startsWith("mqtt://"))
    {
        Logger.debug("MQTT.ConnectionString: " + String(connectionString));

        int indexOfArroba = connectionString.indexOf('@');
        if (indexOfArroba != -1)
        {
            String usernameAndPassword = connectionString.substring(7, indexOfArroba);
            Logger.debug(usernameAndPassword);

            int indexOfSeparator = usernameAndPassword.indexOf(':');
            Logger.debug(String(indexOfSeparator));
            String username = usernameAndPassword.substring(0, indexOfSeparator);
            const char *mqttUserName = username.c_str();
            Logger.debug(mqttUserName);

            String password = usernameAndPassword.substring(indexOfSeparator + 1);
            const char *mqttPassword = password.c_str();
            Logger.debug(mqttPassword);

            if (mqtt->connect(mqttUserName, mqttPassword))
            {
                mqtt->setCallback(callback);
                mqtt->subscribe("/home/bathroom/relay1/status");
                mqtt->subscribe("/home/bathroom/relay1/on");
                mqtt->subscribe("/home/bathroom/relay1/off");
                mqtt->subscribe("/home/bathroom/relay2/status");
                mqtt->subscribe("/home/bathroom/relay2/on");
                mqtt->subscribe("/home/bathroom/relay2/off");

                return true;
            }
        }
    }
    return false;
}

bool initMQTT()
{
    String connectionString = Settings.readMqttConnectionString();
    if (connectionString.length() > 0 && connectionString.startsWith("mqtt://"))
    {
        Logger.debug("MQTT.ConnectionString: " + String(connectionString));

        int indexOfArroba = connectionString.indexOf('@');
        if (indexOfArroba != -1)
        {
            String serverAndPort = connectionString.substring(indexOfArroba + 1);
            Logger.debug(serverAndPort);

            int indexOfSeparator = serverAndPort.indexOf(':');
            Logger.debug(String(indexOfSeparator));
            String server = serverAndPort.substring(0, indexOfSeparator);
            const char *mqttServer = server.c_str();
            Logger.debug(mqttServer);
            uint16_t mqttPort = serverAndPort.substring(indexOfSeparator + 1).toInt();
            Logger.debug(String(mqttPort));

            mqtt = new MqttAdapter("/cullen/baño2", mqttServer, mqttPort);

            return reconnect();
        }
    }
    return false;
}

void setup()
{
    traceFreeMemory();

    initLogger();
    initHardware();

    Logger.debug("Getting ChipID");
    Logger.trace("ChipID: " + String(ESP.getChipId()));

    WifiAdapter.addAP(Settings.readSSID(1), Settings.readPassword(1));
    WifiAdapter.addAP(Settings.readSSID(2), Settings.readPassword(2));

    if (WifiAdapter.connect())
    {
        initMQTT();
        initClock();
    }
    else
    {
        WifiAdapter.startAsAccessPoint("IoT-Device-1234");
    }

    telnetServer = new TelnetServer(23);
    //TODO: Add commands to configure wifi

    Logger.trace("ready");

    digitalWrite(LED_BUILTIN, LOW);
}

void loopMQTT()
{
    if (!mqtt->connected())
    {
        reconnect();
    }
    mqtt->loop();
}

long lastProcess = 0;
void processButtons()
{
    if (button1->pushed())
    {
        Logger.trace("Button 1 was pressed");
        relay1->toggle();
    }

    if (button2->pushed())
    {
        Logger.trace("Button 2 was pressed");
        relay2->toggle();
    }
}

void processRelays()
{
    relay1->process();
    relay2->process();

    Logger.debug("Check completed.");
}

void processTelnet()
{
    if (telnetServer != NULL)
        telnetServer->process();
}

#define DHT_MAX 5
float humidity;
float temperature;
int dhtIndex = 0;

int timeSinceLastRead = 0;
void processDht()
{
    // Report every 2 seconds.
    if (millis() > timeSinceLastRead + dht.getMinimumSamplingPeriod())
    {
        float h = dht.getHumidity();
        float t = dht.getTemperature();

        // Check if any reads failed and exit early (to try again).
        if (isnan(h) || isnan(t))
        {
            Logger.error("Failed to read from DHT sensor!");
            timeSinceLastRead = 0;
            return;
        }

        humidity += h;
        temperature += t;
        Logger.debug("Humidity: " + String(h) + "%\t" + "Temperature: " + String(t) + " *C ");

        timeSinceLastRead = millis();

        if (dhtIndex < DHT_MAX)
        {
            dhtIndex++;
        }
        else
        {
            float humidityAvg = humidity / (dhtIndex + 1);
            float temperatureAvg = temperature / (dhtIndex + 1);

            String message = "{\"humidity\": " + String(humidityAvg) +
                             ", \"temperature\": " + String(temperatureAvg) + "}";
            mqtt->publish("/home/bathroom/dht", message.c_str());

            humidity = 0;
            temperature = 0;
            dhtIndex = 0;
        }
    }
}

void loop(void)
{
    traceFreeMemory();

    loopMQTT();

    processTelnet();

    traceMemoryLeak(&processButtons);

    processDht();

    if (lastProcess + 5000 < millis())
    {
        traceMemoryLeak(&processRelays);
        lastProcess = millis();
    }
}