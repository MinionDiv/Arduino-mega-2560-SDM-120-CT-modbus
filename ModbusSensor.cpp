/*****************************************************************
 Real-time energy Monitoring system for Industrial Machine.
 Data to access through Blynk Legacy
 Creation date:17Dec2021
 Author:Divya
 *******************************************************************/
//------------------------------------------------------------------------------

#define MODBUS_SERIAL_OUTPUT  //Verbose MODBUS messages and timing

#ifdef MODBUS_SERIAL_OUTPUT
#define MODBUS_SERIAL_BEGIN(...) Serial.begin(__VA_ARGS__)
#define MODBUS_SERIAL_PRINT(...) Serial.print(__VA_ARGS__)
#define MODBUS_SERIAL_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define MODBUS_SERIAL_BEGIN(...)
#define MODBUS_SERIAL_PRINT(...)
#define MODBUS_SERIAL_PRINTLN(...)
#endif

#include "ModbusSensor.h"

// Finite state machine status
#define STOP                0
#define SEND                1
#define SENDING             2
#define RECEIVING           3
#define IDLE                4
#define WAITING_NEXT_POLL   5

#define READ_COIL_STATUS          0x01 // Reads the ON/OFF status of discrete outputs (0X references, coils) in the slave.
#define READ_INPUT_STATUS         0x02 // Reads the ON/OFF status of discrete inputs (1X references) in the slave.
#define READ_HOLDING_REGISTERS    0x03 // Reads the binary contents of holding registers (4X references) in the slave.
#define READ_INPUT_REGISTERS      0x04 // Reads the binary contents of input registers (3X references) in the slave. Not writable.
#define FORCE_MULTIPLE_COILS      0x0F // Forces each coil (0X reference) in a sequence of coils to either ON or OFF.
#define PRESET_MULTIPLE_REGISTERS 0x10 // Presets values into a sequence of holding registers (4X references).

#define MB_VALID_DATA     0x00
#define MB_INVALID_ID     0xE0
#define MB_INVALID_FC     0xE1
#define MB_TIMEOUT        0xE2
#define MB_INVALID_CRC    0xE3
#define MB_INVALID_BUFF   0xE4
#define MB_ILLEGAL_FC     0x01
#define MB_ILLEGAL_ADR    0x02
#define MB_ILLEGAL_DATA   0x03
#define MB_SLAVE_FAIL     0x04
#define MB_EXCEPTION      0x05

// when _status is diferent to MB_VALID_DATA change it to zero or hold last valid value?
//#define CHANGE_TO_ZERO    0x00
//#define CHANGE_TO_ONE     0x01
//#define HOLD_VALUE        0xFF


uint16_t calculateCRC(uint8_t *array, uint8_t num) {
  uint16_t temp, flag;
  temp = 0xFFFF;
  for (uint8_t i = 0; i < num; i++) {
    temp = temp ^ array[i];
    for (uint8_t j = 8; j; j--) {
      flag = temp & 0x0001;
      temp >>= 1;
      if (flag)
        temp ^= 0xA001;
    }
  }
  return temp;
}

// Constructor
modbusSensor::modbusSensor(modbusMaster * mbm, uint8_t id, uint16_t adr, uint8_t hold) {
  _frame[0] = id;
  _frame[1] = READ_INPUT_REGISTERS;
  _frame[2] = adr >> 8;
  _frame[3] = adr & 0x00FF;
  _frame[4] = 0x00;
  _frame[5] = 0x02;
  uint16_t crc = calculateCRC(_frame, 6);
  _frame[6] = crc & 0x00FF;
  _frame[7] = crc >> 8;
  _status = MB_TIMEOUT;
  _hold = hold;
  _value.f = 0.0;
  (*mbm).connect(this);
}
/*
  // Constructor
  modbusSensor::modbusSensor(uint8_t id, uint16_t adr, uint8_t hold) {
  _frame[0] = id;
  _frame[1] = READ_INPUT_REGISTERS;
  _frame[2] = adr >> 8;
  _frame[3] = adr & 0x00FF;
  _frame[4] = 0x00;
  _frame[5] = 0x02;
  uint16_t crc = calculateCRC(_frame, 6);
  _frame[6] = crc & 0x00FF;
  _frame[7] = crc >> 8;
  _status = MB_TIMEOUT;
  _hold = hold;
  _value.f = 0.0;
  MBSerial.connect(this);
  }*/



// read value in defined units
float modbusSensor::read() {
  if (_status == MB_TIMEOUT)
    switch (_hold) {
      case CHANGE_TO_ZERO: return 0.0;
      case CHANGE_TO_ONE: return 1.0;
      case HOLD_VALUE: return _value.f;
    }
  return _value.f;
}

// read value as a integer multiplied by factor
uint16_t modbusSensor::read(uint16_t factor) {
  if (_status == MB_TIMEOUT)
    switch (_hold) {
      case CHANGE_TO_ZERO: return (uint16_t) 0;
      case CHANGE_TO_ONE: return (uint16_t) factor;
      case HOLD_VALUE: return (uint16_t)(_value.f * factor);
    }
  return (uint16_t)(_value.f * factor);
}
// get status of the value
inline uint8_t modbusSensor::getStatus() {
  return _status;
}

// write sensor value
inline void modbusSensor::write(float value) {
  _value.f = value;
}

//  put new status
inline uint8_t modbusSensor::putStatus(uint8_t status) {
  _status = status;
  return _status;
}

// get pointer to _poll frame
inline uint8_t *modbusSensor::getFramePtr() {
  return _frame;
}

//---------------------------------------------------------------------------------------//
//---------------------------------------------------------------------------------------//

//constructor
modbusMaster::modbusMaster(HardwareSerial * hwSerial, uint8_t TxEnPin) {
  _state = STOP;
  _TxEnablePin = TxEnPin;
  pinMode(_TxEnablePin, OUTPUT);
  _MBSerial = hwSerial;
  _totalSensors = 0;
  for (uint8_t i = 0; i < MAX_SENSORS; i++)
    _mbSensorsPtr[i] = 0;
}

//------------------------------------------------------------------------------
// Connect a modbusSensor to the modbusMaster array of queries
void modbusMaster::connect(modbusSensor * mbSensor) {
  if (_totalSensors < MAX_SENSORS) {
    _mbSensorsPtr[_totalSensors] = mbSensor;
    _totalSensors++;
  }
  return;
}

//------------------------------------------------------------------------------
// Disconnect a modbusSensor to the modbusMaster array of queries
void modbusMaster::disconnect(modbusSensor * mbSensor) {
  uint8_t i, j;
  for (i = 0;   i < _totalSensors; i++) {
    if (_mbSensorsPtr[i] == mbSensor)  {
      for (j = i; j < _totalSensors - 1; j++) {
        _mbSensorsPtr[j] = _mbSensorsPtr[j + 1];
      }
      _totalSensors--;
      _mbSensorsPtr[_totalSensors] = 0;
    }
  }
}

//------------------------------------------------------------------------------
// begin communication using ModBus protocol over RS485
void modbusMaster::begin(uint16_t baudrate, uint8_t byteFormat, uint16_t pollInterval) {
  _pollInterval = pollInterval - 1;
  if (baudrate > 19200)
    _T2_5 = 1250;
  //_T3_5 = 1750; _T1_5 = 750;
  else
    _T2_5 = 27500000 / baudrate; // 2400 bauds --> 11458 us; 9600 bauds --> 2864 us
  //_T3_5 = 38500000 / baudrate; // number of bits 11 * 3.5 =
  //_T1_5 = 16500000 / baudrate; // 1T * 1.5 = T1.5
  (*_MBSerial).begin(baudrate, byteFormat);
  _state = SEND;
  digitalWrite(_TxEnablePin, LOW);
}

//------------------------------------------------------------------------------
// end communication over serial port
inline void modbusMaster::end() {
  _state = STOP;
  (*_MBSerial).end();
}

//------------------------------------------------------------------------------
// Finite State Machine core,
boolean modbusMaster::available() {
  static uint8_t  indexSensor = 0;                // index of arrray of sensors
  static uint8_t  frameSize;                      // size of the answer frame
  static uint32_t tMicros;                        // time to check between characters in a frame
  static uint32_t nowMillis = millis();
  static uint32_t lastPollMillis = nowMillis;     // time to check poll interval
  static uint32_t sendMillis = nowMillis;         // time to check timeout interval
  static uint32_t receiveMillis = nowMillis;      // time to check waiting interval

  switch (_state) {
    //-----------------------------------------------------------------------------
    case SEND:

      if (indexSensor < _totalSensors) {
        _mbSensorPtr = _mbSensorsPtr[indexSensor];
        _framePtr = (*_mbSensorPtr).getFramePtr();
        digitalWrite(_TxEnablePin, HIGH);
        sendFrame();

        _state = SENDING;
        return false;
      }
      else {
        indexSensor = 0;
        _state = WAITING_NEXT_POLL;
        return true;
      }

    //-----------------------------------------------------------------------------
    case SENDING:

      if ((*_MBSerial).availableForWrite() == SERIAL_TX_BUFFER_SIZE - 1) { //TX buffer empty
        delayMicroseconds(_T2_5); // time to be sure last byte sended
        while ((*_MBSerial).available()) (*_MBSerial).read(); // clean RX buffer
        digitalWrite(_TxEnablePin, LOW);
        sendMillis = millis(); //starts  slave's timeOut
        _state = RECEIVING;
        frameSize = 0;
      }
      return false;

    //-----------------------------------------------------------------------------
    case RECEIVING:

      if (!(*_MBSerial).available()) {
        if (millis() - sendMillis > TIMEOUT) {
          (*_mbSensorPtr).putStatus(MB_TIMEOUT);
          indexSensor++;
          _state = SEND;
        }
        return false;
      }

      if ((*_MBSerial).available() > frameSize) {
        frameSize++;
        tMicros = micros();
      }
      else {
        if (micros() - tMicros > _T2_5) {
          readBuffer(frameSize);
          // MODBUS_SERIAL_PRINTLN((*_mbSensorPtr).getStatus(), HEX);
          indexSensor++;
          receiveMillis = millis(); //starts waiting interval to next request
          _state = IDLE;
        }
      }
      return false;

    //-----------------------------------------------------------------------------
    case IDLE:
      if (millis() - receiveMillis > WAITING_INTERVAL)
        _state = SEND;
      return false;

    //-----------------------------------------------------------------------------
    case WAITING_NEXT_POLL:
      nowMillis = millis();
      if ((nowMillis - lastPollMillis) > _pollInterval) {
        lastPollMillis = nowMillis;
        _state = SEND;
      }
      return false;

    //-----------------------------------------------------------------------------
    case STOP:   // do nothing

      return false;

  }
}

//-----------------------------------------------------------------------------
inline void modbusMaster::readBuffer(uint8_t frameSize) {
  uint8_t index = 0;
  //  boolean ovfFlag = false;
  // MODBUS_SERIAL_PRINT(millis());
  // MODBUS_SERIAL_PRINT("  SLAVE:");
  for (index = 0; index < frameSize; index++) {
    _buffer[index] = (*_MBSerial).read();
//    #ifdef MODBUS_SERIAL_OUTPUT
//        if (_buffer[index] < 0x10)
//          Serial.print(F(" 0"));
//        else
//          Serial.print(F(" "));
//        Serial.print(_buffer[index], HEX);
//    #endif
  }
  // MODBUS_SERIAL_PRINT(" ");
  // MODBUS_SERIAL_PRINTLN(millis());

  // The minimum buffer size from a slave can be an exception response of 5 bytes.
  // If the buffer was partially filled set a frame_error.
  if (frameSize < 5) {
    (*_mbSensorPtr).putStatus(MB_SLAVE_FAIL);
    return;
  }

  if (_buffer[0] != _framePtr[0]) {
    (*_mbSensorPtr).putStatus(MB_INVALID_ID);
    return;
  }

  uint16_t crc = calculateCRC(_buffer, index - 2);
  if (_buffer[frameSize - 1] != crc >> 8 && _buffer[frameSize - 2] != crc & 0x00FF) {
    (*_mbSensorPtr).putStatus(MB_INVALID_CRC);
    return;
  }
  if (_buffer[1] & 0x80 == 0x80) {
    (*_mbSensorPtr).putStatus(_buffer[2]); // see exception codes in define area
    return;
  }

  if (_buffer[1] != _framePtr[1]) {
    (*_mbSensorPtr).putStatus(MB_INVALID_FC);
    return;
  }

  switch (_buffer[1]) {
    case READ_INPUT_REGISTERS:
      if (_buffer[2] == 4) {
        dataFloat temp;
        temp.array[3] = _buffer[3];
        temp.array[2] = _buffer[4];
        temp.array[1] = _buffer[5];
        temp.array[0] = _buffer[6];
        // MODBUS_SERIAL_PRINTLN(temp.f);
        (*_mbSensorPtr).write(temp.f);
        (*_mbSensorPtr).putStatus(MB_VALID_DATA);
        return;
      }
      else {
        (*_mbSensorPtr).putStatus(MB_ILLEGAL_DATA);
        return;
      }
    default:
      (*_mbSensorPtr).putStatus(MB_INVALID_FC);
      return;
  }
}

//-----------------------------------------------------------------------------
//
inline void modbusMaster::sendFrame() {
  // MODBUS_SERIAL_PRINT(millis());
  // MODBUS_SERIAL_PRINT(F(" MASTER:"));

  (*_MBSerial).write(_framePtr, 8);

  #ifdef MODBUS_SERIAL_OUTPUT
    for (uint8_t i = 0; i < 8; i++) {
//      if (_framePtr[i] < 0x10)
//        Serial.print(F(" 0"));
//      else
//        Serial.print(F(" "));
//      Serial.print(_framePtr[i], HEX);
    }
  #endif
  // MODBUS_SERIAL_PRINT("    ");
  // MODBUS_SERIAL_PRINTLN(millis());

}
