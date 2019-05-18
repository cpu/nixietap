#include <Arduino.h>
#include <nixie.h>
#include <NixieAPI.h>
#include <BQ32000RTC.h>
#include <NtpClientLib.h> // Development version is used: https://github.com/gmag11/NtpClient/tree/develop
#include <TimeLib.h>
#include <WiFiManager.h> // Development version is used: https://github.com/tzapu/WiFiManager/tree/development
#include <EEPROM.h>
#include <Ticker.h>
// This service locates the nearest NTP server from your location and attempts to synchronize the date (UTC time) from it.
// Another option, if you live in the United States, is to use the NIST Internet Clock with the name of the server: "time.nist.gov".
#define NTP_SERVER "pool.ntp.org"

// When testing the code you should leave DEBUG defined if you want to see messages on the serial monitor.
#define DEBUG

void irq_1Hz_int();         // Interrupt function for changing the dot state every 1 second.
void buttonPressed();       // Interrupt function when button is pressed.
void enableSecDot();
void disableSecDot();
void scrollDots();          // Interrupt function for scrolling dots.
void checkForAPInvoke();    // Checks if the user tapped 5 times in a rapid succession. If yes, invokes AP mode.
void syncParameters();
void readParameters();
void processSyncEvent(NTPSyncEvent_t ntpEvent);
void cryptoRefresh();
void weatherRefresh();

volatile bool dot_state = LOW;
bool resetDone = true, stopDef = false, secDotDef = false;
bool wifiFirstConnected = false, syncEventTriggered = false; // True if a time event has been triggered.
int16_t timeZone = 0, minutesTimeZone = 0;
uint16_t yearInt = 0;
uint8_t monthInt = 0, dayInt = 0, hoursInt = 0, minutesInt = 0, timeFormat = 1, weatherFormat = 1, dst = 0, firstSyncEvent = 1;
volatile uint8_t state = 0, tuchState = 0, dotPosition = 0b10, weatherRefreshFlag = 1, cryptoRefreshFlag = 1;
char WMTimeFormat[3] = "", WMYear[6] = "", WMMonth[4] = "", WMDay[4] = "", WMHours[4] = "", WMMinutes[4] = "", WMweatherFormat[3] = "";
char tzdbKey[50] = "", stackKey[50] = "", googleLKey[50] = "", googleTZkey[50] = "", currencyID[6] = "", weatherKey[50] = "";
unsigned long previousMillis = 0;
time_t prevTime = 0;        // The last time when the nixie tubes were sync. This prevents the change of the nixie tubes unless the time has changed.
String cryptoCurrencyPrice = "", temperature = "";

NTPSyncEvent_t ntpEvent;    // Last triggered event.
Ticker movingDot, priceRefresh, temperatureRefresh; // Initializing software timer interrupt called movingDot and priceRefresh.
WiFiManager wifiManager;
// Initialization of parameters for manual configuration of time and date.
WiFiManagerParameter text1("<h1><center>Manual time adjustment</center></h1>");
WiFiManagerParameter text2("<p><b>Please fill in all fields with the current date and time: </b></p>");
WiFiManagerParameter yearWM("Year", "Year: ", WMYear, 4);
WiFiManagerParameter monthWM("Month", "Month: (1-January,..., 12-December)", WMMonth, 2);
WiFiManagerParameter dayWM("Day", "Day: (From 1 to 31)", WMDay, 2);
WiFiManagerParameter hoursWM("Hours", "Hours: (24h format)", WMHours, 2);
WiFiManagerParameter minutesWM("Minutes", "Minutes: ", WMMinutes, 2);
WiFiManagerParameter formatWM("TimeFormat", "Time format(24h=1/12h=0): ", WMTimeFormat, 2);
WiFiManagerParameter text3("<h1><center>Configuration of API Key</center></h1>");
WiFiManagerParameter text4("<p><b>Please fill in the field for which you have a key:</b></p>");
WiFiManagerParameter timezonedbKey("Key_1", "TimezoneDB API Key: ", tzdbKey, 50);
WiFiManagerParameter ipStackKey("Key_2", "IPstack API Key: ", stackKey, 50);
WiFiManagerParameter googleLocKey("Key_3", "Google Geolocation API Key: ", googleLKey, 50);
WiFiManagerParameter googleTimeZoneKey("Key_4", "Google Time Zone API Key: ", googleTZkey, 50);
WiFiManagerParameter openWeatherMapKey("Key_5", "Open Weather Map API Key: ", weatherKey, 50);
WiFiManagerParameter openWeatherMapFormat("WeatherFormat", "Please select weather format(Metric=1/Imperial=0): ", WMweatherFormat, 2);
WiFiManagerParameter text5("<p><b>All entered parameters will be permanently saved until they are replaced with the new one.</b></p>");
WiFiManagerParameter text6("<h1><center>Configuration of crypto currency ID</center></h1>");
WiFiManagerParameter cryptoID("cryptoID", "Enter cryptocurrency ID: (Example: Bitcoin: 1, Litecoin: 2, Ethereum: 1027, Ripple: 52): ", currencyID, 4);
WiFiManagerParameter text7("<p>NixieTap uses the <b>CoinMarketCap</b> API service to get the price of the cryptocurrencies.</p>");
WiFiManagerParameter text8("<p>For a complete list of cryptocurrency IDs visit: <b>https://api.coinmarketcap.com/v2/listings/</b></p>");

void setup() {
    // Touch button interrupt.
    attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPressed, RISING);
    #ifdef DEBUG
        delay(3000);    // To have time to open a serial monitor.
    #endif // DEBUG
    readParameters(); // Reed all stored parameters from EEPROM.
    
    // WiFiManager. For configuring WiFi access point, setting up the NixieTap parameters and so on...
    // Sets timeout(in seconds) until configuration portal gets turned off.
    wifiManager.setConfigPortalTimeout(800);
    // Adding parameters to Settings window in WiFiManager AP.
    wifiManager.addParameter(&text1);
    wifiManager.addParameter(&text2);
    wifiManager.addParameter(&yearWM);
    wifiManager.addParameter(&monthWM);
    wifiManager.addParameter(&dayWM);
    wifiManager.addParameter(&hoursWM);
    wifiManager.addParameter(&minutesWM);
    wifiManager.addParameter(&formatWM);
    wifiManager.addParameter(&text3);
    wifiManager.addParameter(&text4);
    wifiManager.addParameter(&timezonedbKey);
    wifiManager.addParameter(&ipStackKey);
    wifiManager.addParameter(&googleLocKey);
    wifiManager.addParameter(&googleTimeZoneKey);
    wifiManager.addParameter(&openWeatherMapKey);
    wifiManager.addParameter(&openWeatherMapFormat);
    wifiManager.addParameter(&text5);
    wifiManager.addParameter(&text6);
    wifiManager.addParameter(&cryptoID);
    wifiManager.addParameter(&text7);
    wifiManager.addParameter(&text8);
    // Determining the look of the WiFiManager Web Server, which buttons will be visible on a main tab.
    std::vector<const char *> menu = {"wifi","param","info","sep","erase","exit"};
    wifiManager.setMenu(menu);

    movingDot.attach(0.2, scrollDots); // This is the software timer interrupt which calls function scrollDots every 0,2s.
    priceRefresh.attach(300, cryptoRefresh); // This will refresh the cryptocurrency price every 5min.
    temperatureRefresh.attach(3600, weatherRefresh);
    
    // Fetches ssid and pass from eeprom and tries to connect,
    // if it does not connect it starts an access point with the specified name "NixieTapAP"
    // and goes into a blocking loop awaiting configuration.
    if(!wifiManager.autoConnect("NixieTap", "Nixie123")) {
        #ifdef DEBUG
            Serial.println("Failed to connect or AP is manually closed!");
        #endif // DEBUG
        // If the NixieTap is not connected to WiFi, it will collect the entered parameters and configure the RTC according to them.
        wifiFirstConnected = false;
    } else {
        #ifdef DEBUG
            Serial.println("NixieTap is connected to WiFi!");
        #endif // DEBUG
        wifiFirstConnected = true;
    }
    syncParameters();  // Collects the entered parameters in the WiFiManager AP and saves them.
}

void loop() {
    if(wifiFirstConnected) { // Every time NixieTap connects to the new WiFi point, it only preforms this part of a code once.
        wifiFirstConnected = false;
        // Configuring NTP server.
        NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
            ntpEvent = event; 
            syncEventTriggered = true;
        });
        // While the time is not first adjusted yet, synchronization will be attempted every 15 seconds. When first sync is done, UTC time will be synced every hour.
        NTP.setInterval(20, 3600);
        NTP.setNTPTimeout(6000);
        NTP.begin(NTP_SERVER, timeZone, false, minutesTimeZone);
        enableSecDot();
    }
    if(syncEventTriggered) {
        // When the syncEvent is triggered depending on a set interval of synchronization this function will be executed.
        processSyncEvent(ntpEvent);
        syncEventTriggered = false;
    }
    checkForAPInvoke(); // This function allows you to manually start the access point on demand. (By tapping the button 5 times in a rapid succesion)
    
    // When the button is pressed nixie tubes will change the displaying mode from time to date, and vice verse. 
    if(state >= 4) state = 0;
    switch(state) {
        case 0 : // Display time.
                if(now() != prevTime) { // Update the display only if time has changed.
                    prevTime = now();
                    nixieTap.writeTime(now(), dot_state, timeFormat);
                }
                break;
        case 1 : // Display date.
                nixieTap.writeDate(now(), 1);
                break;
        case 2 : // Cryptocurrency price
                if(currencyID[0] != '\0') {    // If the currency is not selected, this step will be skipped.
                    if(cryptoRefreshFlag) {
                        cryptoRefreshFlag = 0;
                        cryptoCurrencyPrice = nixieTapAPI.getCryptoPrice(currencyID);
                    }
                    nixieTap.writeNumber(cryptoCurrencyPrice, 350);
                } else state++;
                break;
        case 3 :  // Display temperature.
                if(weatherKey[0] != '\0') {
                    if(weatherRefreshFlag) {
                        weatherRefreshFlag = 0;
                        String loc = nixieTapAPI.getLocation();
                        if(loc != "0") {
                            temperature = nixieTapAPI.getTempAtMyLocation(loc, weatherFormat);
                        } else state++;
                    }
                    nixieTap.writeNumber(temperature, 0);
                } else state++;
                break;
        default:       
                #ifdef DEBUG
                    Serial.println("Error. Unknown state of a button!");
                #endif // DEBUG
                break;
    }
}

void checkForAPInvoke() {
    // By tapping the button 5 times in a time gap of a 900 ms. You can manually start the WiFi Manager and access its settings.
    if(tuchState == 1) previousMillis = millis();
    if((tuchState >= 5) && ((millis() - previousMillis) <= 900)) {
        resetDone = false;
        tuchState = 0;
        if(!resetDone) {
            resetDone = true;
            nixieTap.write(10, 10, 10, 10, 0);
            disableSecDot(); // If dots are not disabled, precisely RTC_IRQ_PIN interrupt, ConfigPortal will chrach.
            movingDot.attach(0.2, scrollDots);
            #ifdef DEBUG
                Serial.println("---------------------------------------------------------------------------------------------");
            #endif // DEBUG
            wifiManager.setConfigPortalTimeout(800);
            // This will run a new config portal if the conditions are met.
            if(!wifiManager.startConfigPortal("NixieTap", "Nixie123")) {
                #ifdef DEBUG
                    Serial.println("Failed to connect or AP is manually closed!");
                #endif // DEBUG
                // If the NixieTap is not connected to WiFi, it will collect the entered parameters and configure the RTC according to them.
                if(WiFi.status() == WL_CONNECTED) {
                    wifiFirstConnected = true;
                } else { 
                    wifiFirstConnected = false;
                }
                syncParameters();
            } else {
                #ifdef DEBUG
                    Serial.println("NixieTap is connected to WiFi!");
                #endif // DEBUG
                wifiFirstConnected = true;
            }
            movingDot.detach();
            enableSecDot();
        }
    } else if((millis() - previousMillis) > 1000) tuchState = 0;
}

void processSyncEvent(NTPSyncEvent_t ntpEvent) {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
    #endif // DEBUG
    if(ntpEvent) {
        #ifdef DEBUG
            Serial.print("Time Sync error: ");
        #endif // DEBUG
        if(ntpEvent == noResponse) {
            #ifdef DEBUG
                Serial.println("NTP server not reachable.");
            #endif // DEBUG
        } else if(ntpEvent == invalidAddress) {
            #ifdef DEBUG
                Serial.println("Invalid NTP server address.");
            #endif // DEBUG
        }
        movingDot.detach(); // Stops scrolling dots.
        nixieTap.write(1, 1, 1, 1, 0); // Display error code.
        #ifdef DEBUG
            Serial.println("Synchronization will be attempted again after 20 seconds.");
            Serial.println("If the time is not synced after 2 minutes, please restart Nixie Tap and try again!");
            Serial.println("If restart does not help. There might be a problem with the NTP server or your WiFi connection. You can set the time manually.");
        #endif // DEBUG
            delay(2000);
    } else {
        #ifdef DEBUG
            Serial.println("NTP time is obtained: " + NTP.getTimeStr());
        #endif // DEBUG
        // Modifies UTC depending on the selected time zone. 
        // After that the time is sent to the RTC and Time library.
        timeZone = nixieTapAPI.getTimezone(now(), &dst);
        if(timeZone != 0 || dst != 0) {
            NTP.setTimeZone((timeZone/60), (timeZone%60));
            NTP.setDayLight(dst);
        }
        RTC.set(now());
        if(firstSyncEvent) {
            movingDot.detach();                // Stops scrolling dots.
            nixieTap.write(10, 10, 10, 10, 0); // Deletes remaining dot on display.
        }
    }
}
void readParameters() {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Reading saved parameters from EEPROM.");
    #endif
    EEPROM.begin(266);
    int EEaddress = 0;
    EEPROM.get(EEaddress, tzdbKey);
    if(tzdbKey[0] != '\0')
        nixieTapAPI.applyKey(tzdbKey, 0);
    EEaddress += sizeof(tzdbKey);
    EEPROM.get(EEaddress, stackKey);
    if(stackKey[0] != '\0')
        nixieTapAPI.applyKey(stackKey, 1);
    EEaddress += sizeof(stackKey);
    EEPROM.get(EEaddress, googleLKey);
    if(googleLKey[0] != '\0')
        nixieTapAPI.applyKey(googleLKey, 2);
    EEaddress += sizeof(googleLKey);
    EEPROM.get(EEaddress, googleTZkey);
    if(googleTZkey[0] != '\0')
        nixieTapAPI.applyKey(googleTZkey, 3);
    EEaddress += sizeof(googleTZkey);
    EEPROM.get(EEaddress, weatherKey);
    if(weatherKey[0] != '\0')
        nixieTapAPI.applyKey(weatherKey, 4);
    EEaddress += sizeof(weatherKey);
    EEPROM.get(EEaddress, timeFormat);
    EEaddress += sizeof(timeFormat);
    EEPROM.get(EEaddress, currencyID);
    EEaddress += sizeof(currencyID);
    EEPROM.get(EEaddress, weatherFormat);
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Saved API Keys from EEPROM: ");
        if(tzdbKey[0] != '\0')
            Serial.println("  Timezonedb Key: " + String(tzdbKey));
        if(stackKey[0] != '\0')
            Serial.println("  Ipstack Key: " + String(stackKey));
        if(googleLKey[0] != '\0')
            Serial.println("  Google Location Key: " + String(googleLKey));
        if(googleTZkey[0] != '\0')
            Serial.println("  Google Time Zone Key: " + String(googleTZkey));
        if(weatherKey[0] != '\0')
            Serial.println("  OneWeaterMap Key: " + String(weatherKey));
        if(currencyID[0] != '\0')
            Serial.println("  Cryptocurrency ID: " + String(currencyID));
        Serial.printf("  Weather format is (Celsius=1/Fahrenheit=0): %d\n", weatherFormat);
        Serial.printf("  Time format is (24h=1/12h=0): %d\n", timeFormat);
        Serial.println("---------------------------------------------------------------------------------------------");
    #endif // DEBUG
    EEPROM.end();
}
void syncParameters() {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Starting synchronization of parameters.");
    #endif // DEBUG
    EEPROM.begin(266); // Number of bytes to allocate for parameters.
    int EEaddress = 0;
    char newKey[50];
    bool newTime = false;
    // Store updated parameters to local memory.
    strcpy(WMYear, yearWM.getValue());
    strcpy(WMMonth, monthWM.getValue());
    strcpy(WMDay, dayWM.getValue());
    strcpy(WMHours, hoursWM.getValue());
    strcpy(WMMinutes, minutesWM.getValue());

    strcpy(newKey, timezonedbKey.getValue());
    #ifdef DEBUG
        Serial.println("Comparing entered keys with saved ones.");
    #endif // DEBUG
    if(strcmp(newKey, tzdbKey) && newKey[0] != '\0') {   // If the keys are different, old key will be replaced with the new one.
        strcpy(tzdbKey, newKey);
        nixieTapAPI.applyKey(tzdbKey, 0);
        EEPROM.put(EEaddress, tzdbKey);
    }
    EEaddress += sizeof(tzdbKey);
    strcpy(newKey, ipStackKey.getValue());
    if(strcmp(newKey, stackKey) && newKey[0] != '\0') {
        strcpy(stackKey, newKey);
        nixieTapAPI.applyKey(stackKey, 1);
        EEPROM.put(EEaddress, stackKey);
    }
    EEaddress += sizeof(stackKey);
    strcpy(newKey, googleLocKey.getValue());
    if(strcmp(newKey, googleLKey) && newKey[0] != '\0') {
        strcpy(googleLKey, newKey);
        nixieTapAPI.applyKey(googleLKey, 2);
        EEPROM.put(EEaddress, googleLKey);
    }
    EEaddress += sizeof(googleLKey);
    strcpy(newKey, googleTimeZoneKey.getValue());
    if(strcmp(newKey, googleTZkey) && newKey[0] != '\0') {
        strcpy(googleTZkey, newKey);
        nixieTapAPI.applyKey(googleTZkey, 3);
        EEPROM.put(EEaddress, googleTZkey);
    }
    EEaddress += sizeof(googleTZkey);
    strcpy(newKey, openWeatherMapKey.getValue());
    if(strcmp(newKey, weatherKey) && newKey[0] != '\0') {
        strcpy(weatherKey, newKey);
        nixieTapAPI.applyKey(weatherKey, 4);
        EEPROM.put(EEaddress, weatherKey);
    }
    EEaddress += sizeof(weatherKey);
    strcpy(WMTimeFormat, formatWM.getValue());
    uint8_t newTimeFormat = atoi(WMTimeFormat);
    if(WMTimeFormat[0] != '\0' && timeFormat != newTimeFormat && (newTimeFormat == 1 || newTimeFormat == 0)) {
        timeFormat = newTimeFormat;
        EEPROM.put(EEaddress, timeFormat);
    }
    EEaddress += sizeof(timeFormat);
    char WMcurrencyID[6];
    strcpy(WMcurrencyID, cryptoID.getValue());
    uint16_t newCurrencyID = atoi(WMcurrencyID);
    if(WMcurrencyID[0] != '\0' && currencyID != WMcurrencyID && newCurrencyID >= 1 && newCurrencyID <= 9999) {
        cryptoRefreshFlag = 1;
        strcpy(currencyID, WMcurrencyID);
        EEPROM.put(EEaddress, currencyID);
    }
    EEaddress += sizeof(currencyID);
    strcpy(WMweatherFormat, formatWM.getValue());
    uint8_t newWeatherFormat = atoi(WMweatherFormat);
    if(WMweatherFormat[0] != '\0' && weatherFormat != newWeatherFormat && (newWeatherFormat == 1 || newWeatherFormat == 0)) {
        weatherRefreshFlag = 1;
        weatherFormat = newWeatherFormat;
        EEPROM.put(EEaddress, weatherFormat);
    }
    EEPROM.end();
    
    // Convert parameters from char to int.
    if(yearInt != atoi(WMYear) && WMYear[0] != '\0') {
        yearInt = atoi(WMYear);
        newTime = true;
    }
    if(monthInt != atoi(WMMonth) && WMMonth[0] != '\0') {
        monthInt = atoi(WMMonth);
        newTime = true;
    }
    if(dayInt != atoi(WMDay) && WMDay[0] != '\0') {
        dayInt = atoi(WMDay);
        newTime = true;
    }
    if(hoursInt != atoi(WMHours) && WMHours[0] != '\0') {
        hoursInt = atoi(WMHours);
        newTime = true;
    }
    if(minutesInt != atoi(WMMinutes) && WMMinutes[0] != '\0') {
        minutesInt = atoi(WMMinutes);
        newTime = true;
    }
    // Check if the parameters are changed. If so, then enter in manual date and time configuration mode.
    if (newTime) {
        // Basic check for entered data. It is not thorough, there is still room for improvement.
        if(nixieTap.checkDate(yearInt, monthInt, dayInt, hoursInt, minutesInt)) {
            setTime(hoursInt, minutesInt, 0, dayInt, monthInt, yearInt);
            RTC.set(now());
            NTP.stop();     // NTP sync is disableded to avoid sync errors.
            movingDot.detach();                // Stops scrolling dots.
            nixieTap.write(10, 10, 10, 10, 0); // Deletes remaining dot on display.
            enableSecDot();
        } else {
            #ifdef DEBUG
                Serial.println("Incorect date and time parameters! Please restart your device or tap 5 times on NixieTap case and try again.");
            #endif // DEBUG
        }
    }
}
/*                                                           *
 *  Enables the center dot to change its state every second. *
 *                                                           */
void enableSecDot() {
    if(secDotDef == false) {
        detachInterrupt(RTC_IRQ_PIN);
        RTC.setIRQ(1);              // Configures the 512Hz interrupt from RTC.
        attachInterrupt(digitalPinToInterrupt(RTC_IRQ_PIN), irq_1Hz_int, FALLING);
        secDotDef = true;
        stopDef = false;
    }
}
/*                                                *
 * Disaling the dots function on nixie display.   *
 *                                                */
void disableSecDot() {
    if(stopDef == false) {
        detachInterrupt(RTC_IRQ_PIN);
        RTC.setIRQ(0);              // Configures the interrupt from RTC.
        dotPosition = 0b10;         // Restast dot position.
        stopDef = true;
        secDotDef = false;
    }
}
/*                                                                                       *
 * An interrupt function that changes the state and position of the dots on the display. *
 *                                                                                       */
void scrollDots() {
    if(dotPosition == 0b100000) dotPosition = 0b10;
    nixieTap.write(11, 11, 11, 11, dotPosition);
    dotPosition = dotPosition << 1;
}
/*                                                                  *
 * An interrupt function for changing the dot state every 1 second. *
 *                                                                  */
void irq_1Hz_int() {
    dot_state = !dot_state;
}
/*                                                                *
 * An interrupt function for the touch sensor when it is touched. *
 *                                                                */
void buttonPressed() {
    tuchState++;
    state++;
}
void cryptoRefresh() {
    cryptoRefreshFlag = 1;
}
void weatherRefresh() {
    weatherRefreshFlag = 1;
}