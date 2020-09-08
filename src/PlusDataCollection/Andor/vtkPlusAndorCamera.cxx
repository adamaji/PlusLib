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
  if(this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusAndorCamera. Cannot proceed.");
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
  this->RequirePortNameInDeviceSetConfiguration = true;

  // We will acquire the frames sporadically,
  // and usually with long exposure times.
  // We don't want polling to interfere with it.
  this->StartThreadForInternalUpdates = false;

  unsigned result = Initialize("");
  if(result != DRV_SUCCESS)
  {
    LOG_ERROR("Andor SDK could not be initialized");
  }
  else
  {
    LOG_DEBUG("Andor SDK initialized.");
  }
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
  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  unsigned result = GetTemperatureRange(&MinTemp, &MaxTemp);
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

  this->GetVideoSourcesByPortName("BLIraw", BLIraw);
  this->GetVideoSourcesByPortName("BLIrectified", BLIrectified);
  this->GetVideoSourcesByPortName("GrayRaw", GrayRaw);
  this->GetVideoSourcesByPortName("GrayRectified", GrayRectified);
  if (BLIraw.size()+BLIrectified.size()+GrayRaw.size()+GrayRectified.size()==0)
  {
      vtkPlusDataSource* aSource = nullptr;
      if (this->GetFirstActiveOutputVideoSource(aSource) != PLUS_SUCCESS || aSource == nullptr)
      {
          LOG_ERROR("Standard data sources are not defined, and unable to retrieve the video source in the capturing device.");
          return PLUS_FAIL;
      }
      BLIraw.push_back(aSource); // this is the default port
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");
  if(IsRecording())
  {
    this->InternalStopRecording();
  }

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
  unsigned long frameSize = xSize * ySize / (AndorHbin * AndorVbin);
  rawFrame.resize(frameSize, 0);

  unsigned result = StartAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "StartAcquisition")
  result = WaitForAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "WaitForAcquisition")

  // iKon-M 934 has 16-bit digitization
  // https://andor.oxinst.com/assets/uploads/products/andor/documents/andor-ikon-m-934-specifications.pdf
  // so we choose 16-bit unsigned
  // GetMostRecentImage() is 32 bit signed variant
  result = GetMostRecentImage16(&rawFrame[0], frameSize);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetMostRecentImage16")

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireBLIFrame()
{
  WaitForCooldown();
  AcquireFrame();

  // add it to the data source
  // and if OpenCV is available to rectified data source

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
