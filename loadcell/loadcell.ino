#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HX711.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Font4x7Fixed.h>

//  Yeet for ESP32 later.
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ElegantOTA.h>

#define FS LittleFS

unsigned long loadcell_last = 0;
#define LOADCELL_REFRESHING 500

long weight = 0;
long full_weight = 0;
long remaining = 0;
long reading = 0;  // raw

void i_am_error() {
	Serial.flush();
	ESP.deepSleep(0);
}

Adafruit_SSD1306 display(-1);  // Reset pin is not on a GPIO on Wemos D1 OLED Shield

void setup_oled(const char *cfgfile) {
	//  could configure font and maybe i2c address
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.clearDisplay();
	display.setTextColor(WHITE);

	display.setCursor(0, 0);
	display.println("setup");
	display.display();  //  HURR DURRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
}

unsigned long oled_last = 0;
unsigned long oled_state = 0;
#define OLED_REFRESHING 1000

void oled_refresh() {
	if (millis() - oled_last > OLED_REFRESHING) {
		oled_last = millis();

		oled_state = oled_state % 3;  //  max states

		if (oled_state == 0) {
			display.clearDisplay();
			display.setFont(&Font4x7Fixed);
			display.setTextWrap(false);
			display.setTextSize(1);
			display.setCursor(0, 7);  //  custom fonts go "up"?
			display.println(WiFi.localIP());

			display.setFont();  //  return to system font
			display.setTextWrap(true);
			display.setTextSize(2);
			display.setCursor(0, 8);
			display.println(weight);
		}
		if (oled_state == 1) {
			display.clearDisplay();
			display.setFont(&Font4x7Fixed);
			display.setTextWrap(false);
			display.setTextSize(1);
			display.setCursor(0, 7);  //  custom fonts go "up"?
			display.println("REMAINING");

			display.setFont();  //  return to system font
			display.setTextWrap(true);
			display.setTextSize(2);
			display.setCursor(0, 8);
			display.println(remaining);
		}
		if (oled_state == 2) {
			display.clearDisplay();
			display.setFont(&Font4x7Fixed);
			display.setTextWrap(false);
			display.setTextSize(1);
			display.setCursor(0, 7);  //  custom fonts go "up"?
			display.println("FULL");

			display.setFont();  //  return to system font
			display.setTextWrap(true);
			display.setTextSize(2);
			display.setCursor(0, 8);
			display.println(full_weight);
		}

		oled_state++;

		display.display();
	}
}

HX711 loadcell;
unsigned long loadcell_state = 0;

unsigned long max_filament_weight = 0;
unsigned long mount_weight = 0;

float conversion_a, conversion_b, conversion_c = 0.f;

void loadcell_refresh() {
	if (!loadcell_state) {
		return;
	}
	if (millis() - loadcell_last > LOADCELL_REFRESHING) {
		loadcell_last = millis();

		if (loadcell.wait_ready_timeout(250)) {
			reading = loadcell.get_units(10);  //  some sort of async would be bettr but whatevs
			// i legit do not know WTF this library is doing fr fr
			weight = conversion_a + conversion_b*reading + conversion_c*reading*reading;
			remaining = max_filament_weight - (full_weight - weight - mount_weight);
		}
		else {
			Serial.println("ur load bad mang");
		}
	}
}

boolean loadcell_cfg_valid(const JsonDocument &doc) {
	//  seems like there should be a json validator somewhere...  maybe another file?
	const char *reqKeys[] = {
		"pin_dout",
		"pin_sck",
		"max_filament_weight",
		"mount_weight",
		"conversion_a",
		"conversion_b",
		"conversion_c"
	};
	
	for (auto key : reqKeys) {
		if (!doc.containsKey(key)) {
			Serial.print(key);
			Serial.println(" not found in config");
			return(false);
		}
	}

	return(true);
}

void setup_loadcell(const char *cfgfile) {
	File file = FS.open(cfgfile,"r");

	//JsonDocument doc;  v7-ism?

	StaticJsonDocument<2048> cfg;

	DeserializationError error = deserializeJson(cfg, file);
	file.close();

	if (error || !loadcell_cfg_valid(cfg)) {
		Serial.print(cfgfile);
		Serial.println(F(" error"));
		return;
	}

	//  yeah could do with a lot more error checking here

	//  DOUT is an input; to be safe don't put it on a bootstrap pin...
	loadcell.begin(cfg["pin_dout"], cfg["pin_sck"]);  //  kewl

	loadcell.set_scale(1.f);  // dispensing with this hamhanded shit
	loadcell.set_offset(0);

	max_filament_weight = cfg["max_filament_weight"];
	mount_weight = cfg["mount_weight"];

	conversion_a = strtof(String(cfg["conversion_a"]).c_str(), NULL);  //  lolc++ i actually don't know wtf the type actually is
	conversion_b = strtof(String(cfg["conversion_b"]).c_str(), NULL);
	conversion_c = strtof(String(cfg["conversion_c"]).c_str(), NULL);

	loadcell_state = 1;
}

AsyncWebServer webserver(80);

String html_processor(const String& var) {
	if (var == "weight") {
		return String(weight);
	}
	if (var == "reading") {
		return String(reading);
	}

	if (var == "full_weight") {
		return String(full_weight);
	}

	if (var == "remaining") {
		return String(remaining);
	}

        return String();
}

//  how can haz checksum and a buttload of error checking?
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
	if (!index) {
		request->_tempFile = FS.open("/www/_tempFile","w");
	}
	if (len) {
		request->_tempFile.write(data, len);
	}
	if (final) {
		request->_tempFile.close();
		//  move to dest file once successful
		if (filename == "config.json") {
			FS.rename("/www/_tempFile", "/www/config.json");
		}
		request->redirect("/");
	}
}

boolean wifi_cfg_valid(const JsonDocument &doc) {

	const char *reqKeys[] = {
		"ssid",
		"psk",
		"hostname",
	};

	for (auto key : reqKeys) {
		if (!doc.containsKey(key)) {
			Serial.print(key);
			Serial.println(" not found in config");
			return(false);
		}
	}

	return(true);
}

void setup_wifi(const char *cfgfile) {
	File file = FS.open(cfgfile,"r");

	StaticJsonDocument<2048> doc;

	DeserializationError error = deserializeJson(doc, file);
	file.close();

	if (error || !wifi_cfg_valid(doc)) {
		Serial.print(cfgfile);
		Serial.println(F(" error"));
		i_am_error();
	}

	WiFi.persistent(false);  //  starting to have flashbacks regarding hostnames, mdns, and light sleep.
	WiFi.hostname(String(doc["hostname"]));
	WiFi.mode(WIFI_STA);

	WiFi.begin(String(doc["ssid"]), String(doc["psk"]));
	while (WiFi.status() != WL_CONNECTED) {
		delay(1000);
	}

	Serial.println(WiFi.localIP());

	webserver.serveStatic("/", FS, "/www/").setDefaultFile("index.html").setTemplateProcessor(html_processor);

	webserver.on("/action_page.php", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebParameter *p = NULL;
		if (request->hasParam("cmd")) {
			p = request->getParam("cmd");

			if (p->value().compareTo("set_full_weight") == 0) {
				if (request->hasParam("full_weight")) {
					//  todo:  need error checking
					full_weight =  strtoul(request->getParam("full_weight")->value().c_str(), NULL, 10);  //  lolc++
				}
				request->redirect("/");
			}
		}
		request->redirect("/");
	});

	webserver.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
		request->send(200);
	}, handleUpload);

	webserver.begin();

	ElegantOTA.begin(&webserver);

}

void setup() {
	Serial.begin(115200);
	Serial.println("\n");
	delay(1000);

	setup_oled("");  //  oled mostly has no dependencies n doesn't crash so go first

	if (!FS.begin()) {
		Serial.println(F("ur fs is borken mang"));
		i_am_error();
	}

	setup_wifi("/wifi.json");
	setup_loadcell("/www/config.json");
}

void loop() {
	ElegantOTA.loop();
	loadcell_refresh();
	oled_refresh();
	delay(100);
}
