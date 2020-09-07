/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkPlusAndorCamera.h"
#include "ATMCD32D.h"
#include "igtlOSUtil.h" // for Sleep

#define AndorCheckErrorValueAndFailIfNeeded(returnValue, functionName) \
  if(returnValue != DRV_SUCCESS)                                       \
  {                                                                    \
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << " with error code: " << returnValue);                 \
    return PLUS_FAIL;                                                  \
  }

vtkStandardNewMacro(vtkPlusAndorCamera);

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Shutter: " << AndorShutter << std::endl;
  os << indent << "ExposureTime: " << AndorExposureTime << std::endl;
  os << indent << "HSSpeed: " << AndorHSSpeed[0] << AndorHSSpeed[1] << std::endl;
  os << indent << "PreAmpGain: " << AndorPreAmpGain << std::endl;
  os << indent << "AcquisitionMode: " << AndorAcquisitionMode << std::endl;
  os << indent << "ReadMode: " << AndorReadMode << std::endl;
  os << indent << "TriggerMode: " << AndorTriggerMode << std::endl;
  os << indent << "Hbin: " << AndorHbin << std::endl;
  os << indent << "Vbin: " << AndorVbin << std::endl;
  os << indent << "CoolTemperature: " << AndorCoolTemperature << std::endl;
  os << indent << "SafeTemperature: " << AndorSafeTemperature << std::endl;
  os << indent << "CurrentTemperature: " << AndorCurrentTemperature << std::endl;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusAndorCamera::ReadConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  // Load the camera properties parameters -----------------------------------------------

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, Shutter, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(float, ExposureTime, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, PreAmpGain, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AcquisitionMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, ReadMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, TriggerMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, Hbin, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, Vbin, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, CoolTemperature, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, SafeTemperature, deviceConfig);

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);

  deviceConfig->SetIntAttribute("Shutter", this->AndorShutter);
  deviceConfig->SetFloatAttribute("ExposureTime", this->AndorExposureTime);
  deviceConfig->SetIntAttribute("PreAmpGain", this->AndorPreAmpGain);
  deviceConfig->SetIntAttribute("AcquisitionMode", this->AndorAcquisitionMode);
  deviceConfig->SetIntAttribute("ReadMode", this->AndorReadMode);
  deviceConfig->SetIntAttribute("TriggerMode", this->AndorTriggerMode);
  deviceConfig->SetIntAttribute("Hbin", this->AndorHbin);
  deviceConfig->SetIntAttribute("Vbin", this->AndorVbin);
  deviceConfig->SetIntAttribute("CoolTemperature", this->AndorCoolTemperature);
  deviceConfig->SetIntAttribute("SafeTemperature", this->AndorSafeTemperature);

  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::NotifyConfigured()
{
  if(this->OutputChannels.size() > 1)
  {
    LOG_WARNING("vtkPlusAndorCamera is expecting one output channel and there are "
                << this->OutputChannels.size() << " channels. First output channel will be used.");
  }

  if(this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusIntersonVideoSource. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
std::string vtkPlusAndorCamera::GetSdkVersion()
{
  std::ostringstream versionString;

  unsigned int epromVer;
  unsigned int cofVer;
  unsigned int driverRev;
  unsigned int driverVer;
  unsigned int dllRev;
  unsigned int dllVer;

  unsigned int andorResult = GetSoftwareVersion(&epromVer, &cofVer, &driverRev, &driverVer, &dllRev, &dllVer);

  versionString << "Andor SDK version: "  << dllVer << "." << dllRev << std::endl;
  return versionString.str();
}

// ------------------------------------------------------------------------
// Protected member operators ---------------------------------------------

//----------------------------------------------------------------------------
vtkPlusAndorCamera::vtkPlusAndorCamera()
{
  this->RequireImageOrientationInConfiguration = true;

  // No callback function provided by the device,
  // so the data capture thread will be used
  // to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates = true;
  this->AcquisitionRate = 1;
}

// ----------------------------------------------------------------------------
vtkPlusAndorCamera::~vtkPlusAndorCamera()
{
  if(!this->Connected)
  {
    this->Disconnect();
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InitializeAndorCamera()
{
  unsigned int result = Initialize("");
  AndorCheckErrorValueAndFailIfNeeded(result, "Initialize Andor SDK")
  LOG_DEBUG("Andor SDK initialized.");

  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  result = GetTemperatureRange(&MinTemp, &MaxTemp);
  if(MaxTemp < this->AndorSafeTemperature)
  {
    this->AndorSafeTemperature = MaxTemp;
  }
  LOG_INFO("The temperature range for the connected Andor Camera is: " << MinTemp << " and " << MaxTemp);

  if(this->AndorCoolTemperature < MinTemp || this->AndorCoolTemperature > MaxTemp)
  {
    LOG_ERROR("Requested temperature for Andor camera is out of range");
    return PLUS_FAIL;
  }

  result = CoolerON();
  AndorCheckErrorValueAndFailIfNeeded(result, "Turn Andor Camera Cooler on")

  result = SetTemperature(this->AndorCoolTemperature);
  AndorCheckErrorValueAndFailIfNeeded(result, "Set Andor Camera cool temperature")

  GetCurrentTemperature(); // logs the status and temperature

  result = GetDetector(&xSize, &ySize);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetDetectorSize")
  frameBuffer.resize(xSize * ySize / (AndorHbin*AndorVbin));

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalConnect()
{
  LOG_TRACE("vtkPlusAndorCamera::InternalConnect");
  if(this->InitializeAndorCamera() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Setup the camera
  this->SetShutter(this->AndorShutter);
  this->SetExposureTime(this->AndorExposureTime);
  this->SetPreAmpGain(this->AndorPreAmpGain);
  this->SetAcquisitionMode(this->AndorAcquisitionMode);
  this->SetReadMode(this->AndorReadMode);
  this->SetTriggerMode(this->AndorTriggerMode);
  this->SetHbin(this->AndorHbin);
  this->SetVbin(this->AndorVbin);
  this->SetCoolTemperature(this->AndorCoolTemperature);
  this->SetSafeTemperature(this->AndorSafeTemperature);

  // Prepare acquisition

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");

  int status;
  unsigned result = IsCoolerOn(&status);
  AndorCheckErrorValueAndFailIfNeeded(result, "IsCoolerOn")

  if(status)
  {
    GetCurrentTemperature(); // updates this->AndorCurrentTemperature
    if(this->AndorCurrentTemperature < this->AndorSafeTemperature)
    {
      LOG_INFO("Temperature yet not at a safe point, turning the Cooler Off");
      result = CoolerOFF();
      AndorCheckErrorValueAndFailIfNeeded(result, "CoolerOff")

      while(this->AndorCurrentTemperature < this->AndorSafeTemperature)
      {
        igtl::Sleep(5000); // wait a bit
        GetCurrentTemperature(); // logs the status and temperature
      }
    }
  }

  result = FreeInternalMemory();
  AndorCheckErrorValueAndFailIfNeeded(result, "FreeInternalMemory")

  result = ShutDown();
  AndorCheckErrorValueAndFailIfNeeded(result, "ShutDown")

  LOG_INFO("Andor camera shut down successfully");
  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStartRecording()
{
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStopRecording()
{
  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetCurrentTemperature()
{
  unsigned result = GetTemperatureF(&this->AndorCurrentTemperature);
  switch(result)
  {
    case DRV_TEMPERATURE_STABILIZED:
      LOG_INFO("Temperature has stabilized at " << this->AndorCurrentTemperature << " °C");
      break;
    case DRV_TEMPERATURE_NOT_REACHED:
      LOG_INFO("Cooling down, current temperature is " << this->AndorCurrentTemperature << " °C");
      break;
    default:
      LOG_INFO("Current temperature is " << this->AndorCurrentTemperature << " °C");
      break;
  }

  return this->AndorCurrentTemperature;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::WaitForCooldown()
{
  while(GetTemperatureF(&this->AndorCurrentTemperature) != DRV_TEMPERATURE_STABILIZED)
  {
    igtl::Sleep(1000); // wait a bit
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireFrame()
{
  unsigned result = StartAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "StartAcquisition")
  result = WaitForAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "WaitForAcquisition")

  // iKon-M 934 has 16-bit digitization
  // https://andor.oxinst.com/assets/uploads/products/andor/documents/andor-ikon-m-934-specifications.pdf
  // so we choose 16-bit unsigned
  // GetMostRecentImage() is 32 bit signed variant
  result = GetMostRecentImage16(&frameBuffer[0], xSize * ySize / (AndorHbin * AndorVbin));
  AndorCheckErrorValueAndFailIfNeeded(result, "GetMostRecentImage16")

  return PLUS_SUCCESS;
}




// Setup the Andor camera parameters ----------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetShutter(int shutter)
{
  this->AndorShutter = shutter;
  unsigned int result = ::SetShutter(1, this->AndorShutter, 0, 0);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetShutter")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetShutter()
{
  return this->AndorShutter;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetExposureTime(float exposureTime)
{
  this->AndorExposureTime = exposureTime;

  unsigned int result = ::SetExposureTime(this->AndorExposureTime);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetExposureTime")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetExposureTime()
{
  return this->AndorExposureTime;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetPreAmpGain(int preAmpGain)
{
  this->AndorPreAmpGain = preAmpGain;

  unsigned int result = ::SetPreAmpGain(this->AndorPreAmpGain);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetPreAmpGain")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetPreAmpGain()
{
  return this->AndorPreAmpGain;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAcquisitionMode(int acquisitionMode)
{
  this->AndorAcquisitionMode = acquisitionMode;

  unsigned int result = ::SetAcquisitionMode(this->AndorAcquisitionMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetAcquisitionMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAcquisitionMode()
{
  return this->AndorAcquisitionMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetReadMode(int readMode)
{
  this->AndorReadMode = readMode;

  unsigned int result = ::SetReadMode(this->AndorReadMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetReadMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetReadMode()
{
  return this->AndorReadMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetTriggerMode(int triggerMode)
{
  this->AndorTriggerMode = triggerMode;

  unsigned int result = ::SetTriggerMode(this->AndorTriggerMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetTriggerMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetTriggerMode()
{
  return this->AndorTriggerMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetHbin(int hbin)
{
  this->AndorHbin = hbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetHbin()
{
  return this->AndorHbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetVbin(int vbin)
{
  this->AndorVbin = vbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetVbin()
{
  return this->AndorVbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetCoolTemperature(int coolTemp)
{
  this->AndorCoolTemperature = coolTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetCoolTemperature()
{
  return this->AndorCoolTemperature;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetSafeTemperature(int safeTemp)
{
  this->AndorSafeTemperature = safeTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetSafeTemperature()
{
  return this->AndorSafeTemperature;
}
