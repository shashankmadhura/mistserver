#include "output.h"
#include <mist/amf.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/rtmpchunks.h>
#include <mist/url.h>

namespace Mist{

  class OutRTMP : public Output{
  public:
    OutRTMP(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onRequest();
    void sendNext();
    void sendHeader();
    static bool listenMode();
    void requestHandler();
    bool onFinish();

  protected:
    std::string streamOut; ///< When pushing out, the output stream name
    bool setRtmpOffset;
    int64_t rtmpOffset;
    uint64_t lastOutTime;
    uint32_t maxbps;
    std::string app_name;
    void parseChunk(Socket::Buffer &inputBuffer);
    void parseAMFCommand(AMF::Object &amfData, int messageType, int streamId);
    void sendCommand(AMF::Object &amfReply, int messageType, int streamId);
    void startPushOut(const char *args);
    uint64_t lastAck;
    HTTP::URL pushApp, pushUrl;
    uint8_t authAttempts;
    void sendSilence(uint64_t currTime);
    bool hasSilence;
    uint64_t lastSilence;
  };
}// namespace Mist

typedef Mist::OutRTMP mistOut;
