/*
  xsns_122_vz89te.ino - VZ89TE VOC CO2 gas sensor support for Tasmota

  Copyright (C) 2022  Jean-Claude Lugeon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_VZ89TE
/*********************************************************************************************\
 * Sensirion I2C VOC CO2 sensor
 *
 * This driver supports the following sensors:
 * - VZ89TE: (addresses: 0x70)
\*********************************************************************************************/

#define XSNS_122           122
#define XI2C_100           100          // See I2CDEVICES.md

#define VZ89TE_ADDRESS     0x70         // 0x70
#define VZ89TE_TIMEOUT_MS  30           // 30 miliseconds
//#define EVERYNSECONDS 5

uint8_t VZ89TE_type = 0;
char log_data[180];
float co2;
float voc;
char sVoc[10];
char sCo2[10];

const char HTTP_SNS_VZ89TE[] PROGMEM =
  "{s}VZ89TE " D_ECO2 "{m}%s " D_UNIT_PARTS_PER_MILLION "{e}"                // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>
  "{s}VZ89TE " D_TVOC "{m}%s " D_UNIT_PARTS_PER_BILLION "{e}";


uint8_t Vz89xComputeCrc(uint8_t data[], uint8_t len) {
  // Compute CRC as per datasheet
  uint8_t crc = 0;
  uint8_t byteCtr;
  uint16_t sum = 0;
  for (byteCtr = 0; byteCtr < len; ++byteCtr) {
    sum = crc + data[byteCtr];
    crc = (uint8_t)sum;
    crc += (sum/0x100);
  }
  return 0xff - crc;
}

bool Vz89xRead(uint8_t i2c_address) {
  uint8_t data[7];
  uint8_t i = 0;

  voc = NAN;
  co2 = NAN;

  // Send command to read data
  Wire.beginTransmission(i2c_address);
  Wire.write(0x0C);  // Command to get data
  Wire.write(0x0);
  Wire.write(0x0);
  Wire.write(0x0);
  Wire.write(0x0);
  Wire.write(0xF3);
  if (Wire.endTransmission() != 0) {
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE-%d: transmission error"), i2c_address);
    AddLog(LOG_LEVEL_INFO, log_data);
    return false;
  }
 
  delay(VZ89TE_TIMEOUT_MS);  // Timing verified with logic analyzer (10 is too short)

  // Read response from master
  if (Wire.requestFrom(i2c_address, (uint8_t)7) != 7) {
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE-%d: request from failed"), i2c_address);
    AddLog(LOG_LEVEL_INFO, log_data);
    return false;
  }

  // Read all 7 bytes
  for (i = 0; i < 7; i++) {
    data[i] = Wire.read();
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE index:%d data:0x%X"), i, data[i]);
    AddLog(LOG_LEVEL_DEBUG, log_data);
  }

 // Verify CRC
  if (Vz89xComputeCrc(&data[0], 6) != data[6]) {
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE-%d: CRC error"), i2c_address);
    AddLog(LOG_LEVEL_INFO, log_data);
    return false;
  }

  // Calculate values
  voc = (float)(data[0] - 13) * (1000.0 / 229.0);
  co2 = (float)(data[1] - 13) * (1600.0 / 229.0) + 400.0;
  dtostrfd(voc, 2, sVoc);
  dtostrfd(co2, 2, sCo2);
  snprintf_P(log_data, sizeof(log_data), PSTR("\"VZ89TE\":{%s,\"%s\"}"), sCo2, sVoc);
  AddLog(LOG_LEVEL_DEBUG, log_data);
  return (!isnan(voc) && !isnan(co2));
}

/********************************************************************************************/

void Vz89xDetect(void) {

  // Check if I2C device is found at address
  if (!I2cSetDevice(VZ89TE_ADDRESS)) {
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE-%d: I2C device not found at address"), VZ89TE_ADDRESS);
    AddLog(LOG_LEVEL_INFO, log_data);
    return; 
  }

//  Vz89xRead(voc, co2, VZ89TE_ADDRESS);
  Vz89xRead(VZ89TE_ADDRESS);
  if (Vz89xRead(VZ89TE_ADDRESS)) {
    VZ89TE_type = 1;
    I2cSetActiveFound(VZ89TE_ADDRESS, "VZ89TE");
  } else {
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_I2C "VZ89TE-%d Detect error"), VZ89TE_ADDRESS);
    AddLog(LOG_LEVEL_INFO, log_data);
  }
}

void Vz89xShow(bool json) {

  if (Vz89xRead(VZ89TE_ADDRESS)) {
    if (json) {
      ResponseAppend_P(PSTR(",\"VZ89TE\":{\"" D_JSON_ECO2 "\":%s,\"" D_JSON_TVOC "\":%s}"), sCo2, sVoc);

#ifdef USE_DOMOTICZ
      if (0 == TasmotaGlobal.tele_period) DomoticzSensor(DZ_AIRQUALITY, co2);
#endif  // USE_DOMOTICZ
#ifdef USE_WEBSERVER
    } else {
      WSContentSend_PD(HTTP_SNS_VZ89TE, sCo2, sVoc);
#endif
    }
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns122(uint32_t function) {

  if (!I2cEnabled(XI2C_100)) { return false; }

  bool result = false;

  if (FUNC_INIT == function) {
    Vz89xDetect();
  }
  else if (VZ89TE_type) {
    switch (function) {
      case FUNC_JSON_APPEND:
        Vz89xShow(1);
        break;
  #ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        Vz89xShow(0);
        break;
  #endif  // USE_WEBSERVER
      case FUNC_COMMAND:
        result = true;
        break;
    }
  }
  return result;
}

#endif  // USE_VZ89XTE
#endif  // USE_I2C
