#include <M5CoreS3.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "passwords.h"            // local

#define ARDUINOJSON_STRING_LENGTH_SIZE 4
#include <ArduinoJson.h>
#include <base64.h>

#include "AudioFileSourceID3.h"
#include "AudioFileSourceSD.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorMP3.h"
#include <AudioOutput.h>

AudioFileSourceSD *file;
AudioFileSourcePROGMEM *progmem;
AudioFileSourceID3 *id3;
AudioGeneratorMP3 *mp3Player = NULL;

String socialMediaServer = "https://www.powerofmoo.com/_functions/socialmedia";
String whisperServer = "https://api.openai.com/v1/audio/transcriptions";
String openAIServer = "https://api.openai.com/v1/chat/completions";
String elevenLabsServer = "https://api.elevenlabs.io/v1/text-to-speech";

String elevenVoiceID = "2SUKdqx6YKcgi9ifvDmw";  // Osirion
const char* openAIquery = "image from a cam, you help the blind, what do you see?";

///////////////////////
// recording voice
///////////////////////

static constexpr const size_t record_number     = 1024;
static constexpr const size_t record_length     = 320;
static constexpr const size_t record_size       = record_number * record_length;
static constexpr const size_t record_samplerate = 17000;
static size_t rec_record_idx  = 0;
static int16_t *rec_data;
static uint8_t *wavHeader;

File audioFile;
bool recordingVoice = false;

///////////////////////
// logo
///////////////////////

uint8_t *logo = NULL;
size_t logo_len;
String logoFile = "/a-eye/logo/logo320x200a.jpg";

uint8_t *logoBlink = NULL;
size_t logoBlink_len;
String logoBlinkFile = "/a-eye/logo/logo320x200x.jpg";

long blinkInterval = 4000; // Time between blinks (in milliseconds)
long blinkRandom = 1000; // added random time for blinks (in milliseconds)
long blinkDuration = 50;  // Duration of the blink (in milliseconds)
long lastBlinkTime = 0;

// MAIN
int BLUEPIN = 8;   // port A - 8/9, port B - 
int REDPIN = 9;
bool redWasPressed = false, blueWasPressed = false;

uint8_t* mp3Data = NULL;
int mp3DataLength = 0;

class AudioOutputM5Speaker : public AudioOutput {
public:
  AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) {
    _m5sound = m5sound;
    _virtual_ch = virtual_sound_channel;
  }
  virtual ~AudioOutputM5Speaker(void){};
  virtual bool begin(void) override {
    return true;
  }
  virtual bool ConsumeSample(int16_t sample[2]) override {
    if (_tri_buffer_index < tri_buf_size) {
      _tri_buffer[_tri_index][_tri_buffer_index] = sample[0];
      _tri_buffer[_tri_index][_tri_buffer_index + 1] = sample[1];
      _tri_buffer_index += 2;

      return true;
    }

    flush();
    return false;
  }
  virtual void flush(void) override {
    if (_tri_buffer_index) {
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
      _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
      _tri_buffer_index = 0;
    }
  }
  virtual bool stop(void) override {
    flush();
    _m5sound->stop(_virtual_ch);
    return true;
  }

  const int16_t* getBuffer(void) const {
    return _tri_buffer[(_tri_index + 2) % 3];
  }

protected:
  m5::Speaker_Class* _m5sound;
  uint8_t _virtual_ch;
  static constexpr size_t tri_buf_size = 1536;
  int16_t _tri_buffer[3][tri_buf_size];
  size_t _tri_buffer_index = 0;
  size_t _tri_index = 0;
};

static constexpr uint8_t m5spk_virtual_channel = 0;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);

void showLogo(bool clear = true, bool blink = false)
{
  // clear screen
  if (clear) CoreS3.Display.clear();

  // logo with or without blink
  if (!logo) return;

  if (blink && logoBlink != NULL) {
    CoreS3.Display.drawJpg(logoBlink, logoBlink_len, 0, 40, 320, 200);
  } else {
    CoreS3.Display.drawJpg(logo, logo_len, 0, 40, 320, 200);
  }
}

/////////////////////////////////////////////////////////////////////////////////////
//
//  setup
//
/////////////////////////////////////////////////////////////////////////////////////

void setup(void) {
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    Serial.begin(115200);

    // SD
    Serial.println("Initialize SD...");
    CoreS3.Display.drawString("Initialize SD...", 0, 0);
    while (false == SD.begin(4)) {
      delay(500);
    }
    Serial.println("SD initialized");
    CoreS3.Display.drawString("SD initialized", 0, 10);

     // connect to WiFi
    Serial.println("Connecting to WiFi...");
    CoreS3.Display.drawString("Connecting to WiFi...", 0, 20);

    const char *ssid = "";
    do {  
      // Attempt to connect to each Wi-Fi network in the array
      for (int i = 0; i < sizeof(wifiCredentials) / sizeof(wifiCredentials[0]); i++) {
        ssid = wifiCredentials[i].ssid;
        Serial.print("trying.. ");
        Serial.println(ssid);

        WiFi.begin(ssid, wifiCredentials[i].password);

        // Wait for up to 15 seconds for connection to establish
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
          delay(1000); // Wait 1 second between attempts
          attempts++;
        }

        // Break the for loop if connected
        if (WiFi.status() == WL_CONNECTED) break;
      }
    } while (WiFi.status() != WL_CONNECTED);
    Serial.print("Connected to WiFi ");
    Serial.println(ssid);
    CoreS3.Display.drawString("Connected to WiFi", 0, 30);

    // logo files
    File jpgFile = SD.open(logoFile, FILE_READ);
    if (!jpgFile) {
      Serial.println("Failed to open file for reading");
    } else {

      // Get the size of the file
      logo_len = jpgFile.size();
      logo = (uint8_t *)malloc(logo_len);

      if (!logo) {
        Serial.println("Failed to allocate memory for logo file");
      } else {
        // Read file into logo buffer
        jpgFile.read(logo, logo_len);
      }
      jpgFile.close();
    }

    File jpgBlinkFile = SD.open(logoBlinkFile, FILE_READ);
    if (!jpgBlinkFile) {
      Serial.println("Failed to open file for reading");
    } else {

      // Get the size of the file
      logoBlink_len = jpgBlinkFile.size();
      logoBlink = (uint8_t *)malloc(logoBlink_len);

      if (!logoBlink) {
        Serial.println("Failed to allocate memory for logo blink file");
      } else {
        // Read file into logo buffer
        jpgBlinkFile.read(logoBlink, logoBlink_len);
      }
      jpgBlinkFile.close();
    }

    // alloc for wavHeader (44) and wavData
    wavHeader = (typeof(wavHeader))heap_caps_malloc(44 + record_size * sizeof(int16_t), MALLOC_CAP_8BIT);
    rec_data = (int16_t*) (&wavHeader[44]);
    
    CoreS3.Speaker.setVolume(255);

// camera
  if (!CoreS3.Camera.begin()) {
      CoreS3.Display.drawString("Camera Init Fail",
                                CoreS3.Display.width() / 2,
                                CoreS3.Display.height() / 2);
  }
  CoreS3.Display.drawString("Camera Init Success", CoreS3.Display.width() / 2,
                            CoreS3.Display.height() / 2);

  CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
  // CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_VGA);
  
    // extra buttons
    pinMode(REDPIN, INPUT);
    pinMode(BLUEPIN, INPUT);

    // display
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(WHITE);
    CoreS3.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
    showLogo();

    // auditive feedback - ready
    M5.Speaker.setVolume(50);
    M5.Speaker.tone(2000, 50);
    delay(70);
    M5.Speaker.tone(1000, 50);
    delay(70);
    M5.Speaker.setVolume(255);
}

/////////////////////////////////////////////////////////////////////////////////////
//
//  loop
//
/////////////////////////////////////////////////////////////////////////////////////

bool tutorialMode = false;
bool tutorialRedPlayed = false;
bool tutorialBluePlayed = false;

void loop(void) {
    CoreS3.update();

    // handle playing mp3 - without interruptions
    if (mp3Player != NULL && mp3Player->isRunning()) {
      // CoreS3.Display.fillTriangle(130 - 8, 15 - 8, 130 - 8, 15 + 8, 130 + 8, 15, GREEN);
      // CoreS3.Display.drawString("PLAY", 180, 3);
      if (!mp3Player->loop()) mp3Player->stop();
      return;
    } 

    // clean up mp3 is its not running anymore
    if (mp3Player) {
      if (mp3Data) free(mp3Data);
      mp3Player = NULL;     // some clean up function??
      mp3Data = NULL;
      mp3DataLength = 0;
      showLogo(!tutorialMode);    // clean when tutorial over
    }

    // handle external buttons
    bool redPressed = (digitalRead(REDPIN) == 0);
    bool bluePressed = (digitalRead(BLUEPIN) == 0);
    bool redButtonClicked = false, blueButtonClicked = false, redButtonStarted = false, blueButtonStarted = false;
    bool redReleased = false, blueReleased = false;
    if (!redPressed && redWasPressed) redButtonClicked = true;
    if (!bluePressed && blueWasPressed) blueButtonClicked = true;
    if (redPressed && !redWasPressed) redButtonStarted = true;
    if (bluePressed && !blueWasPressed) blueButtonStarted = true;
    if (!redPressed && redWasPressed) redReleased = true;
    if (!bluePressed && blueWasPressed) blueReleased = true;
    redWasPressed = redPressed;
    blueWasPressed = bluePressed;

    // handle tutorial mode
    if (M5.BtnPWR.wasClicked()) {
      tutorialMode = true;
      tutorialRedPlayed = false;
      tutorialBluePlayed = false;
      playMp3File("/a-eye/tutorial/tutorialIntro.mp3");

      showLogo();
      CoreS3.Display.fillTriangle(80 - 8, 15 - 8, 80 - 8, 15 + 8, 80 + 8, 15, PURPLE);
      CoreS3.Display.drawString("TUTORIAL", 160, 3);
    }

    if (tutorialMode) {
      if (redButtonClicked) {

        CoreS3.Display.fillCircle(280, 15, 8, RED);

        if (!tutorialBluePlayed) {
          playMp3File ("/a-eye/tutorial/tutorialListenFirst.mp3");
          tutorialRedPlayed = true;
        } else {
          playMp3File ("/a-eye/tutorial/tutorialListenLast.mp3");
          tutorialMode = false;
        }
      }
      
      if (blueButtonClicked) {

        CoreS3.Display.fillCircle(250, 15, 8, BLUE);

        if(!tutorialRedPlayed) {
          playMp3File ("/a-eye/tutorial/tutorialSeeFirst.mp3");
          tutorialBluePlayed = true;
        } else {
          playMp3File ("/a-eye/tutorial/tutorialSeeLast.mp3");
          tutorialMode = false;
        }
      }

      // in tutorial mode, disabled all other functions except blink
      blink();

      return;

    }

    if (!redPressed) {
      // read image in every loop to avoid buffering
      if (CoreS3.Camera.get()) { 
        if (blueButtonClicked) {
          getAudioFromPicture();
          showLogo();
        }
      }
      CoreS3.Camera.free(); 
    }

    // screen click functions as flipflop
    bool startRec = false, startPlay = false;
    if (false && M5.Touch.getCount() && M5.Touch.getDetail(0).wasClicked()) {
      recordingVoice = !recordingVoice;
      if (recordingVoice)
        startRec = true;
      else
        startPlay = true;
    }

    // start recording when red button pressed
    if (redButtonStarted || startRec) {
        CoreS3.Speaker.end();
        CoreS3.Mic.begin();

        showLogo();
        CoreS3.Display.fillCircle(130, 15, 8, RED);
        CoreS3.Display.drawString("REC", 180, 3);
    }

    // record as long as red button pressed and as long as there is no overflow
    if ( (redPressed || recordingVoice) && rec_record_idx < record_number) {
        auto data                  = &rec_data[rec_record_idx * record_length];
        if (CoreS3.Mic.record(data, record_length, record_samplerate)) {
            ++rec_record_idx;
        }
    }

    // some noise reduction stuff
    if (CoreS3.BtnPWR.wasClicked()) {
        auto cfg               = CoreS3.Mic.config();
        cfg.noise_filter_level = (cfg.noise_filter_level + 8) & 255;
        CoreS3.Mic.config(cfg);
        showLogo();
        CoreS3.Display.fillCircle(130, 15, 8, WHITE);
        CoreS3.Display.drawString("NF:" + String(cfg.noise_filter_level), 180,
                                  3);
        delay(2000);
        showLogo();

    }
    
    // if red button released or when overflow - save the file and play it back on speaker
    if (redReleased || startPlay || rec_record_idx >= record_number) {

        if (CoreS3.Speaker.isEnabled()) {

            showLogo();
            CoreS3.Display.fillTriangle(130 - 8, 15 - 8, 130 - 8, 15 + 8, 130 + 8, 15, GREEN);
            CoreS3.Display.drawString("PLAY", 180, 3);
 
            while (CoreS3.Mic.isRecording()) {
                delay(1);
            }
            /// Since the microphone and speaker cannot be used at the same
            /// time, turn off the microphone here.
            CoreS3.Mic.end();
    
            // auditive feedback
            M5.Speaker.setVolume(50);
            M5.Speaker.tone(2000, 50);
            delay(70);
            M5.Speaker.tone(1000, 50);
            delay(70);
            M5.Speaker.setVolume(255);

            fillWavHeader(record_samplerate, record_size  * sizeof(int16_t));

            // data to speaker
            int length = rec_record_idx * record_length;
            CoreS3.Speaker.begin();
            CoreS3.Speaker.playRaw(rec_data, length, record_samplerate, false, 1, 0);

            // send to OpenAI - whisper
            HTTPClient httpWhisper;
            httpWhisper.setTimeout(30000);

            httpWhisper.begin(whisperServer);
            httpWhisper.addHeader("Authorization", "Bearer " + String(openAIapiKey));

            String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW"; // Replace with any unique string

            // Prepare the body
            String bodyStart = "--" + boundary + "\r\n";
            bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n";
            bodyStart += "Content-Type: audio/wav\r\n\r\n";
            
            String bodyMiddle = "\r\n--" + boundary + "\r\n";
            bodyMiddle += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
            bodyMiddle += "whisper-1\r\n";
            
            String bodyEnd = "--" + boundary + "\r\n";
            bodyEnd += "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
            bodyEnd += "text\r\n";
            bodyEnd += "--" + boundary + "--\r\n";

            // Calculate the content length
            int contentLength = bodyStart.length() + bodyMiddle.length() + bodyEnd.length() + 44 + length * sizeof(int16_t);

            Serial.print("Sending multiform payload to whisper with ");
            Serial.print(contentLength);
            Serial.println(" bytes");

            // Set headers
            httpWhisper.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
            httpWhisper.addHeader("Content-Length", String(contentLength));

            // Allocate buffer for payload
            uint8_t* payload = (uint8_t*)malloc(contentLength);
            if (!payload) {
              Serial.println("Failed to allocate buffer for payload");
              return;
            }

            // Copy body part 1 into payload
            int pos = 0;
            memcpy(payload + pos, bodyStart.c_str(), bodyStart.length());
            pos += bodyStart.length();

            // Copy wav content into payload
            memcpy(payload + pos, wavHeader, 44 + length * sizeof(int16_t));
            pos += 44 + length * sizeof(int16_t);

            // Copy body part 2 and 3 into payload
            memcpy(payload + pos, bodyMiddle.c_str(), bodyMiddle.length());
            pos += bodyMiddle.length();
            memcpy(payload + pos, bodyEnd.c_str(), bodyEnd.length());

            // POST to OpenAi - whisper
            int httpCode = httpWhisper.POST(payload, contentLength);
            free(payload);

            // signal if sound is over
            if (!CoreS3.Speaker.isPlaying()) {
              showLogo();
              CoreS3.Display.fillCircle(100, 15, 8, PURPLE);
              CoreS3.Display.drawString("UPLOAD", 180, 3);
            }

            if (httpCode == HTTP_CODE_OK) {
              String response = httpWhisper.getString();
              Serial.println("Whisper Transcription: ");
              Serial.println(response);

              // send to Power of Moo server / social media
              HTTPClient httpSocial;
              httpSocial.setTimeout(30000);

              // Text string to be sent as a parameter
              String textString = urlencode(response);
    
              // Append the text string as a query parameter to the URL
              String requestURL = String(socialMediaServer) + "?message=" + textString;

              // Begin the GET request
              httpSocial.begin(requestURL);

              // signal if sound is over
              if (!CoreS3.Speaker.isPlaying()) {
                showLogo();
                CoreS3.Display.fillCircle(120, 15, 8, PURPLE);
                CoreS3.Display.drawString("UPLOAD", 180, 3);
              }

              // Send the GET request
              int httpResponseCode = httpSocial.GET();
    
              // Check the response code
              if (httpResponseCode > 0) {
                // Get the response payload
                String response = httpSocial.getString();
                Serial.println("Response code: " + String(httpResponseCode));
                Serial.println("Response: " + response);
              } else {
                Serial.println("Error on HTTP request Social Media Server");
              }
              httpSocial.end();
            } else {
              Serial.print("Whisper Failed, error: ");
              Serial.println(httpCode);
            }
            httpWhisper.end();

            // let speaker finish
            do {
                delay(1);
                CoreS3.update();
            } while (CoreS3.Speaker.isPlaying());

            // clean up
            showLogo();
            rec_record_idx = 0;
        }
    }

    if (!redPressed && !recordingVoice) {

      // idle loop
      blink();

      if (!mp3Player) {
        // main buttons
        CoreS3.Display.fillCircle(60, 15, 8, BLUE);
        CoreS3.Display.drawString("SEE", 100, 3);
        CoreS3.Display.fillCircle(180, 15, 8, RED);
        CoreS3.Display.drawString("LISTEN", 240, 3);
      }    
    }
}

void blink()
{
    // do blinking
    long currentMillis = millis();
    if (currentMillis - lastBlinkTime >= blinkInterval) {
      showLogo(false,true);
      delay(blinkDuration);
      showLogo(false);
      lastBlinkTime = millis() + random(0, blinkRandom);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
//
//  helper functions
//
/////////////////////////////////////////////////////////////////////////////////////

void getAudioFromPicture()
{
  // auditive feedback
  M5.Speaker.setVolume(50);
  M5.Speaker.tone(1000, 50);

  // image to JPEG
  uint8_t *out_jpg   = NULL;
  size_t out_jpg_len = 0;
  frame2jpg(CoreS3.Camera.fb, 255, &out_jpg, &out_jpg_len);
  Serial.print("Image size: ");
  Serial.println(out_jpg_len);

  // CoreS3.Display.drawJpg(out_jpg, out_jpg_len, 0, 0, CoreS3.Display.width(), CoreS3.Display.height());
  
  showLogo();
  CoreS3.Display.fillCircle(120, 15, 8, PURPLE);
  CoreS3.Display.drawString("SCAN", 180, 3);

  HTTPClient httpImage;
  httpImage.setTimeout(30000);

  httpImage.begin(openAIServer);
  httpImage.addHeader("Authorization", "Bearer " + String(openAIapiKey));
  httpImage.addHeader("Content-Type", "application/json");

  // Create a JSON object with the image data
  JsonDocument doc;
  doc["model"] = "gpt-4o-mini";
  JsonArray messages = doc.createNestedArray("messages");
  JsonObject message = messages.createNestedObject();
  message["role"] = "user";
  JsonArray content = message.createNestedArray("content");
  JsonObject text = content.createNestedObject();
  text["type"] = "text";
  text["text"] = openAIquery;
  JsonObject image_url = content.createNestedObject();
  image_url["type"] = "image_url";
  JsonObject url_obj = image_url.createNestedObject("image_url");
  url_obj["url"] = "data:image/jpeg;base64," + base64::encode(out_jpg, out_jpg_len);

  // Serialize the JSON object to a string
  int jsonLenPrediction = floor (1.5f * out_jpg_len);     // guessing, base64 = factor 4/3
  char * json = (char *) malloc(jsonLenPrediction);
  serializeJson(doc, json, jsonLenPrediction);
  free(out_jpg);
  if (doc.overflowed() ) {
    free(json);
    Serial.println("Memory error at json/base64 conversion");
    return;
  }

  Serial.println(json);

  // send to OpenAI
  int httpResponseCode = httpImage.POST(json);
  free(json);
  String response = httpImage.getString();

  Serial.println(response);
  httpImage.end();

  Serial.print("OpenAI response (");
  Serial.print(httpResponseCode);
  Serial.println("): ");

  // check for error
  if (httpResponseCode != 200) {
    M5.Speaker.setVolume(100);
    M5.Speaker.tone(400, 200);

    return;
  }

  // auditive feedback
  M5.Speaker.setVolume(50);
  M5.Speaker.tone(2000, 50);

  // Parse the JSON response
  DynamicJsonDocument responseDoc(10240);
  deserializeJson(responseDoc, response);
  String textDesc = responseDoc["choices"][0]["message"]["content"].as<String>();

  // show some output
  Serial.println(textDesc);

  showLogo();
  CoreS3.Display.fillCircle(90, 15, 8, PURPLE);
  CoreS3.Display.drawString("ANALYSE", 180, 3);

  getElevenLabs(textDesc);
   
  // from progmem to speaker
  if (mp3DataLength > 0) {
    progmem = new AudioFileSourcePROGMEM(mp3Data, mp3DataLength);
    id3  = new AudioFileSourceID3(progmem);
    
    M5.Speaker.setVolume(255);

    mp3Player = new AudioGeneratorMP3();
    mp3Player->begin(id3, &out);
    
  }

}

void getElevenLabs(String text)
{
  // 0 means some kind of error along the way
  mp3DataLength = 0;

  // Make the POST request
  HTTPClient httpEleven;
  httpEleven.setTimeout(30000);

  httpEleven.begin(elevenLabsServer + String("/") + elevenVoiceID);
  httpEleven.addHeader("Content-Type", "application/json");
  httpEleven.addHeader("xi-api-key", elevenLabsApiKey);

  // Create the JSON payload with the text
  String payload = "{\"text\": \"" + text + "\"}";

  // Send the POST request
  int httpResponseCode = httpEleven.POST(payload);
  Serial.print("Elevenlabs response (");
  Serial.print(httpResponseCode);
  Serial.println(") ");

   if (httpResponseCode != 200) {
    Serial.println(httpEleven.getString());

    M5.Speaker.setVolume(100);
    M5.Speaker.tone(400, 200);
    httpEleven.end();
    return;
  }
    
  // Get the response
  int length  = httpEleven.getSize();
  
  // Dynamically allocate memory for mp3Data
  mp3Data = (uint8_t*)malloc(length);
  
  if (mp3Data == NULL) {
      Serial.println("Failed to allocate memory for mp3");
      return;
  }

  // Get the stream to read the response data
  WiFiClient* stream = httpEleven.getStreamPtr();

  // Read the mp3Data
  size_t bytesRead = 0;
  while (httpEleven.connected() && (bytesRead < length)) {
      if (stream->available()) {
          bytesRead += stream->readBytes(&mp3Data[bytesRead], length - bytesRead);
      }
  }
  Serial.print("bytes read: ");
  Serial.println(bytesRead);

  mp3DataLength = bytesRead; 

  // Close connection
  httpEleven.end();

}


void fillWavHeader(int sampleRate, int dataSize) {
    uint32_t size = dataSize + 36;
    
    // RIFF chunk descriptor
    memcpy(&wavHeader[0], "RIFF", 4);
    memcpy(&wavHeader[4], &size, 4);         // Chunk size: 4 bytes
    memcpy(&wavHeader[8], "WAVE", 4);
    
    // fmt subchunk
    memcpy(&wavHeader[12], "fmt ", 4);
    uint32_t fmtChunkSize = 16;              // Subchunk size: 4 bytes
    memcpy(&wavHeader[16], &fmtChunkSize, 4);
    
    uint16_t audioFormat = 1;                // Audio format: 2 bytes
    memcpy(&wavHeader[20], &audioFormat, 2);
    
    uint16_t numChannels = 1;                // Mono: 2 bytes
    memcpy(&wavHeader[22], &numChannels, 2);
    
    memcpy(&wavHeader[24], &sampleRate, 4);  // Sample rate: 4 bytes
    
    uint32_t byteRate = sampleRate * numChannels * 2; // Byte rate: 4 bytes
    memcpy(&wavHeader[28], &byteRate, 4);
    
    uint16_t blockAlign = numChannels * 2;   // Block align: 2 bytes
    memcpy(&wavHeader[32], &blockAlign, 2);
    
    uint16_t bitsPerSample = 16;             // Bits per sample: 2 bytes
    memcpy(&wavHeader[34], &bitsPerSample, 2);
    
    // data subchunk
    memcpy(&wavHeader[36], "data", 4);
    memcpy(&wavHeader[40], &dataSize, 4);    // Data size: 4 bytes
}

// Function to URL encode a string
String urlencode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xF) + '0';
      if ((c & 0xF) > 9) {
        code1 = (c & 0xF) - 10 + 'A';
      }
      c = (c >> 4) & 0xF;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

void playMp3File(const char* filename)
{
  file = new AudioFileSourceSD(filename);
  id3  = new AudioFileSourceID3(file);
  Serial.print ("Tutorial playing: ");
  Serial.println(filename);

  M5.Speaker.setVolume(255);

  mp3Player = new AudioGeneratorMP3();
  mp3Player->begin(id3, &out);
}

size_t  freeMemory() {
  return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}