//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-08-30 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// compatible to AskSinPP commit b4f6000
#define MAX_FAKEDEVICE_COUNT  8

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>

#define SYS_LED_PIN        4 // originally 6
#define OK_LED_PIN         7 //not connected
#define ERROR_LED_PIN      8 //not connected

#define RSSI_POLL_INTERVAL 750 //milliseconds

using namespace as;

struct FakeDeviceInfo {
  HMID FakeDeviceID;

  // known timeouts
  // 552, 600, 1200, 3600, 10000, 259200, 40000, 88200, 259200, 1209600
  uint32_t CyclicTimeout;

  bool     Enabled;
  uint32_t CurrentTick;
};

FakeDeviceInfo fakeDevice[MAX_FAKEDEVICE_COUNT];
StatusLed<ERROR_LED_PIN>  errorLed;
#include "HB_MultiChannelDevice.h"
#define CONFIG_BUTTON_PIN  16 //normally 5
#define PEERS_PER_CHANNEL  1
#define CYCLIC_MSG_TIMEOUT 3600  // seconds between sending information about en-/disabled fake devices to ccu

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0xFF, 0x02},          // Device ID
  "SGBEACSNI1",                // Device Serial
  {0xF3, 0xFF},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x01}                 // Info Bytes
};

typedef AskSin<StatusLed<SYS_LED_PIN>, NoBattery, Radio<AvrSPI<10, 11, 12, 13>, 2> > HalType;
HalType hal;
StatusLed<OK_LED_PIN> okLed;

DEFREGISTER(UReg0, MASTERID_REGS, DREG_TRANSMITTRYMAX)
class UList0 : public RegList0<UReg0> {
  public:
    UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}
    void defaults () {
      clear();
      transmitDevTryMax(2);
    }
};

DEFREGISTER(UReg1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08)
class UList1 : public RegList1<UReg1> {
  public:
    UList1 (uint32_t addr) : RegList1<UReg1>(addr) {}

    bool FakeDeviceID (HMID value) const {
      uint8_t v[3];
      memcpy(v, &value, 3);
      return this->writeRegister(0x01, v[0] & 0xff) && this->writeRegister(0x02, v[1] & 0xff) && this->writeRegister(0x03, v[2] & 0xff);
    }

    HMID FakeDeviceID () const {
      return { this->readRegister(0x01, 0), this->readRegister(0x02, 0), this->readRegister(0x03, 0) };
    }

    bool CyclicTimeout (uint32_t value) const {
      return this->writeRegister(0x04, (value >> 16) & 0xff) && this->writeRegister(0x05, (value >> 8) & 0xff) && this->writeRegister(0x06, value & 0xff);
    }
    uint32_t CyclicTimeout () const {
      return ((uint32_t)this->readRegister(0x04, 0) << 16) | ((uint32_t)this->readRegister(0x05, 0) << 8) | (uint32_t)this->readRegister(0x06, 0);
    }

    bool FakeDeviceEnabled () const {
      return this->readBit(0x07, 0, true);
    }
    bool FakeDeviceEnabled (bool v) const {
      return this->writeBit(0x07, 0, v);
    }

    bool StatusValue (uint8_t value) const {
      return this->writeRegister(0x08, value & 0xff);
    }
    uint8_t StatusValue () const {
      return this->readRegister(0x08, 0);
    }

    void defaults () {
      clear();
      FakeDeviceID({0x00, 0x00, 0x00});
      CyclicTimeout(0);
      FakeDeviceEnabled(false);
      StatusValue(0);
    }
};






class DummyDeviceEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, bool enabled) {
      Message::init(0x0b, msgcnt, 0x53, BCAST , channel, enabled);
    }
};

class FakeChannel : public Channel<HalType, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
    DummyDeviceEventMsg   dmsg;

  private:
    uint16_t  _current_tick;
    bool      _last_enabled;

  public:

    FakeChannel () : Channel(), Alarm(0), _current_tick(0), _last_enabled(false) {}
    virtual ~FakeChannel () {}

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      // ****** changes to incorporate sniffer functionality for AskSinAnalyzer ****** //
      set(seconds2ticks(1)); 
      //set(millis2ticks(RSSI_POLL_INTERVAL));
      //device().radio().pollRSSI();
      //DPRINT(":"); DHEX(device().radio().rssi());DPRINTLN(";");
      // ****** end of changes to incorporate sniffer functionality for AskSinAnalyzer ****** //
            
      uint8_t devIdx = number() - 1;

      HMID fakeDevId = fakeDevice[devIdx].FakeDeviceID;

      if (fakeDevId.valid() && fakeDevice[devIdx].Enabled == true) {
        if (fakeDevice[devIdx].CyclicTimeout > 0) {
          fakeDevice[devIdx].CurrentTick++;
          if (fakeDevice[devIdx].CurrentTick >= fakeDevice[devIdx].CyclicTimeout) {
            okLed.ledOn(millis2ticks(200));
            DPRINT(F("SEND MSG FOR DEV ")); fakeDevId.dump(); DPRINTLN("");
            device().sendFakeInfoActuatorStatus(fakeDevId, device().nextcount(), *this);
            fakeDevice[devIdx].CurrentTick = 0;
            _delay_ms(200);
          }
        }
      }

      if (_current_tick >= CYCLIC_MSG_TIMEOUT || _current_tick == 0 || _last_enabled != fakeDevice[devIdx].Enabled) {
        DPRINT(F("ch ")); DDEC(number()); DPRINTLN(F(", BROADCASTING OWN MSG"));
        dmsg.init(device().nextcount(), number(), fakeDevice[devIdx].Enabled);
        device().broadcastPeerEvent(dmsg, *this);
        _delay_ms(200);
        _current_tick = 0;
      }

      _current_tick++;
      _last_enabled = fakeDevice[devIdx].Enabled;
      sysclock.add(*this);
    }

    void configChanged() {
      HMID fdev = this->getList1().FakeDeviceID();
      DPRINT(F("ch ")); DDEC(number()); DPRINT(F(", FakeDeviceID = ")); fdev.dump();  DPRINT(F(", Enabled = ")); DDEC(this->getList1().FakeDeviceEnabled()); DPRINT(F(", CyclicTimeout = ")); DDECLN( this->getList1().CyclicTimeout());
      uint32_t timeout = this->getList1().CyclicTimeout();
      fakeDevice[number() - 1] = { fdev, timeout, this->getList1().FakeDeviceEnabled(), (timeout > 10) ? timeout - 10 : 0 };
    }

    void setup(Device<HalType, UList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);

      sysclock.add(*this);
    }

    uint8_t status () const {
      return this->getList1().StatusValue();
    }

    uint8_t flags () const {
      return 0;
    }
};


template <class DEVTYPE>
class RSSIRec : public Alarm {
  DEVTYPE& device; 
  
  public:
    RSSIRec(DEVTYPE& dev) : device(dev) {sysclock.add(*this);}
    virtual ~RSSIRec () {}
  
    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      tick = millis2ticks(RSSI_POLL_INTERVAL);
      clock.add(*this);
      device.radio().pollRSSI();
      DPRINT(":"); DHEX(device.radio().rssi());DPRINTLN(";");
    }
};


typedef MultiChannelDevice<HalType, FakeChannel, MAX_FAKEDEVICE_COUNT, UList0> UType;
UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);
RSSIRec<UType> rssirec(sdev);


void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  okLed.init();
  errorLed.init();
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    hal.activity.savePower<Idle<>>(hal);
  }
}
