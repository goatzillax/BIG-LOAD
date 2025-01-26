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
	//display.display();  //  HURR DURRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
}

unsigned long oled_last = 0;
#define OLED_REFRESHING 1000
unsigned long oled_state = 0;

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

#define FULL_SPOOL	1000
#define MOUNT_WEIGHT	17

void loadcell_refresh() {
	//Serial.println("reading loadcell");
	if (millis() - loadcell_last > LOADCELL_REFRESHING) {
		loadcell_last = millis();

		if (loadcell.wait_ready_timeout(250)) {
			reading = loadcell.get_units(10);  //  some sort of async would be bettr but whatevs
			// i legit do not know WTF this library is doing fr fr
			//y = -237.3709 - 0.002405105*x + 2.154818e-11*x^2
			weight = 2.154818e-11*reading*reading - 0.002405105f*reading -236.5709f;
			remaining = FULL_SPOOL - (full_weight - weight - MOUNT_WEIGHT);
		}
		else {
			Serial.println("ur load bad mang");
			//  don't want no crashn so we just let it slide
		}

	}
}

void setup_loadcell(const char *cfgfile) {
	File file = FS.open(cfgfile,"r");

	//JsonDocument doc;  v7-ism?

	StaticJsonDocument<2048> loadcell_cfg;

	DeserializationError error = deserializeJson(loadcell_cfg, file);

	if (error) {
		Serial.print(cfgfile);
		Serial.println(F(" error"));
		i_am_error();
	}
	file.close();

	//  SCK is an output?
	//  DOUT is an input; to be safe don't put it on a bootstrap pin...
	loadcell.begin(loadcell_cfg["loadcell"]["pin_dout"], loadcell_cfg["loadcell"]["pin_sck"]);  //  kewl

	//  so to calibrate scale use 1 / 1 + 0 in config.json, then update values

	loadcell.set_scale((float) loadcell_cfg["loadcell"]["cal_reading"] / (float) loadcell_cfg["loadcell"]["cal_weight"]);
	loadcell.set_offset(loadcell_cfg["loadcell"]["offset"]);  //  I'm just not using this.
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

//  todo:  only handle config.json uploading
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
	//  how does this even work?  seems like we should spool it to a temp file and then on file move it in place?
	if(!index){
		Serial.printf("UploadStart: %s\n", filename.c_str());
	}
	for(size_t i=0; i<len; i++){
		Serial.write(data[i]);
	}
	if(final){
		Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index+len);
	}
}

void setup_wifi(const char *cfgfile) {
	File file = FS.open(cfgfile,"r");

	StaticJsonDocument<2048> doc;  //  ssid, psk, hostname

	DeserializationError error = deserializeJson(doc, file);

	if (error) {
		Serial.print(cfgfile);
		Serial.println(F(" error"));
		i_am_error();
	}
	file.close();

	WiFi.persistent(false);  //  starting to have flashbacks regarding hostnames, mdns, and light sleep.
	WiFi.hostname(String(doc["hostname"]));
	WiFi.mode(WIFI_STA);

	WiFi.begin(String(doc["ssid"]), String(doc["psk"]));
	while (WiFi.status() != WL_CONNECTED) {
		delay(1000);
	}

	Serial.println(WiFi.localIP());

//	webserver.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
//		request->send(FS, "/www/index.html", String(), false, html_processor);
//	});

	webserver.serveStatic("/", FS, "/www/").setDefaultFile("index.html").setTemplateProcessor(html_processor);

	webserver.on("/action_page.php", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebParameter *p = NULL;
		if (request->hasParam("cmd")) {
			p = request->getParam("cmd");

			if (p->value().compareTo("set_full_weight") == 0) {
				//  can't reset here because we can't confirm the client has moved off this URL...
				if (request->hasParam("full_weight")) {
					//  need error checking
					full_weight =  strtoul(request->getParam("full_weight")->value().c_str(), NULL, 10);  //  lolc++
				}
				request->redirect("/");
			}
		}
		request->redirect("/");
	});

	webserver.begin();

	ElegantOTA.begin(&webserver);

}

void setup() {
	Serial.begin(115200);
	Serial.println("\n");
	delay(1000);

	if (!FS.begin()) {
		Serial.println(F("ur fs is borken mang"));
		i_am_error();
	}

	setup_oled("");
	setup_loadcell("/www/config.json");
	setup_wifi("/wifi.json");
}

void loop() {
	ElegantOTA.loop();
	loadcell_refresh();
	oled_refresh();
	delay(100);
}
