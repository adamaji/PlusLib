/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/ 

/*!
  \file This test runs a phantom registration on a recorded data set and compares the results to a baseline
*/ 

#include "PlusConfigure.h"
#include "vtkPlusConfig.h"
#include "vtkPhantomRegistrationAlgo.h"
#include "vtkDataCollectorHardwareDevice.h"
#include "vtkTrackedFrameList.h"
#include "vtkFakeTracker.h"
#include "vtkTransformRepository.h"

#include "vtkSmartPointer.h"
#include "vtkXMLDataElement.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx" 
#include "vtksys/SystemTools.hxx"
#include "vtkMatrix4x4.h"
#include "vtkTransform.h"
#include "vtkMath.h"

#include <stdlib.h>
#include <iostream>

///////////////////////////////////////////////////////////////////
const double ERROR_THRESHOLD = 0.05; // error threshold is 5% 

int CompareRegistrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName); 

int main (int argc, char* argv[])
{ 
  std::string inputConfigFileName;
  std::string inputBaselineFileName;

  int verboseLevel=vtkPlusLogger::LOG_LEVEL_DEFAULT;

  vtksys::CommandLineArguments cmdargs;
  cmdargs.Initialize(argc, argv);

  cmdargs.AddArgument("--input-config-file-name", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Configuration file name");
  cmdargs.AddArgument("--input-baseline-file-name", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputBaselineFileName, "Name of file storing baseline calibration results");
  cmdargs.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");  

  if ( !cmdargs.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << cmdargs.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  LOG_INFO("Initialize"); 

  // Read configuration
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(inputConfigFileName.c_str()));
  if (configRootElement == NULL)
  {  
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str()); 
    exit(EXIT_FAILURE);
  }

  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationData(configRootElement); 

  // Stylus and reference tool names
  vtkXMLDataElement* fCalElement = configRootElement->FindNestedElementWithName("fCal"); 
  if (fCalElement == NULL)
  {
    LOG_ERROR("Unable to find fCal element in XML tree!"); 
    exit(EXIT_FAILURE);
  }

  vtkXMLDataElement* trackerToolNames = fCalElement->FindNestedElementWithName("TrackerToolNames"); 
  if (trackerToolNames == NULL)
  {
    LOG_ERROR("Unable to find TrackerToolNames element in XML tree!"); 
    exit(EXIT_FAILURE);
  }
  const char* stylusToolName = trackerToolNames->GetAttribute("Stylus");
  if (stylusToolName == NULL)
  {
    LOG_ERROR("Stylus tool name is not specified in the fCal section of the configuration!");
    exit(EXIT_FAILURE);
  }
  const char* referenceToolName = trackerToolNames->GetAttribute("Reference");
  if (referenceToolName == NULL)
  {
    LOG_ERROR("Reference tool name is not specified in the fCal section of the configuration!");
    exit(EXIT_FAILURE);
  }

  // Initialize data collection
  vtkSmartPointer<vtkDataCollectorHardwareDevice> dataCollector = vtkSmartPointer<vtkDataCollectorHardwareDevice>::New(); 
  if (dataCollector->ReadConfiguration(configRootElement) != PLUS_SUCCESS) {
    LOG_ERROR("Unable to parse configuration from file " << inputConfigFileName.c_str()); 
    exit(EXIT_FAILURE);
  }

  dataCollector->SetTrackingOnly(true);

  if (dataCollector->Connect() != PLUS_SUCCESS) {
    LOG_ERROR("Data collector was unable to connect to devices!");
    exit(EXIT_FAILURE);
  }
  if (dataCollector->Start() != PLUS_SUCCESS) {
    LOG_ERROR("Unable to start data collection!");
    exit(EXIT_FAILURE);
  }
  if (dataCollector->GetTrackingEnabled() == false) {
    LOG_ERROR("Data collector is not tracking!");
    exit(EXIT_FAILURE);
  }

  // Read stylus calibration
  vtkSmartPointer<vtkMatrix4x4> stylusTipToStylusCalibrationMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if ( vtkPlusConfig::ReadTransformToCoordinateDefinition("StylusTip", stylusToolName, stylusTipToStylusCalibrationMatrix) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to read 'StylusTip' to '" << stylusToolName << "' pivot calibration result from configuration file: " << vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName() ); 
    exit(EXIT_FAILURE);
  }
 
  // Create and initialize transform repository
  TrackedFrame trackedFrame;
  dataCollector->GetTrackedFrame(&trackedFrame);

  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New();

  PlusTransformName stylusTipToStylusTransformName("StylusTip", stylusToolName);
  transformRepository->SetTransform(stylusTipToStylusTransformName, stylusTipToStylusCalibrationMatrix);

  transformRepository->SetTransforms(trackedFrame);

  // Check stylus tool
  PlusTransformName stylusToReferenceTransformName(stylusToolName, referenceToolName);
  vtkSmartPointer<vtkMatrix4x4> stylusToReferenceTransformMAtrix = vtkSmartPointer<vtkMatrix4x4>::New();
  bool valid = false;
  transformRepository->GetTransform(stylusToReferenceTransformName, stylusToReferenceTransformMAtrix, &valid);
  if (!valid)
  {
    LOG_ERROR("No valid transform found between stylus to reference!");
    exit(EXIT_FAILURE);
  }

  // Initialize phantom registration
  vtkSmartPointer<vtkPhantomRegistrationAlgo> phantomRegistration = vtkSmartPointer<vtkPhantomRegistrationAlgo>::New();
  if (phantomRegistration == NULL)
  {
    LOG_ERROR("Unable to instantiate phantom registration algorithm class!");
    exit(EXIT_FAILURE);
  }
  if (phantomRegistration->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to read phantom definition!");
    exit(EXIT_FAILURE);
  }

  if (phantomRegistration->GetDefinedLandmarks()->GetNumberOfPoints() != 8)
  {
    LOG_ERROR("Number of defined landmarks should be 8 instead of " << phantomRegistration->GetDefinedLandmarks()->GetNumberOfPoints() << "!");
    exit(EXIT_FAILURE);
  }

  // Acquire landmarks
  vtkFakeTracker *fakeTracker = dynamic_cast<vtkFakeTracker*>(dataCollector->GetTracker());
  if (fakeTracker == NULL) {
    LOG_ERROR("Invalid tracker object!");
    exit(EXIT_FAILURE);
  }

  PlusTransformName stylusTipToReferenceTransformName("StylusTip", referenceToolName);
  PlusTransformName stylusToTrackerTransformName(stylusToolName, "Tracker");

  for (int landmarkCounter=0; landmarkCounter<8; ++landmarkCounter)
  {
    fakeTracker->SetCounter(landmarkCounter);
    vtkAccurateTimer::Delay(1.1 / fakeTracker->GetFrequency());

    vtkSmartPointer<vtkMatrix4x4> stylusTipToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();

    dataCollector->GetTrackedFrame(&trackedFrame);
    transformRepository->SetTransforms(trackedFrame);

    TrackerStatus status = TR_MISSING;
    if ( trackedFrame.GetCustomFrameTransformStatus(stylusToTrackerTransformName, status) == PLUS_SUCCESS
      && status == TR_OK )
    {
      transformRepository->GetTransform(stylusTipToReferenceTransformName, stylusTipToReferenceMatrix, &valid);

      if (valid)
      {
        // Compute point position from matrix
        double elements[16];
        double stylusTipPosition[4];
        for (int i=0; i<4; ++i) for (int j=0; j<4; ++j) elements[4*j+i] = stylusTipToReferenceMatrix->GetElement(i,j);
        double origin[4] = {0.0, 0.0, 0.0, 1.0};
        vtkMatrix4x4::PointMultiply(elements, origin, stylusTipPosition);

        // Add recorded point to algorithm
        phantomRegistration->GetRecordedLandmarks()->InsertPoint(landmarkCounter, stylusTipPosition[0], stylusTipPosition[1], stylusTipPosition[2]);
        phantomRegistration->GetRecordedLandmarks()->Modified();

        vtkPlusLogger::PrintProgressbar((100.0 * landmarkCounter) / 8); 
      }
    }
  }

  if (phantomRegistration->Register() != PLUS_SUCCESS)
  {
    LOG_ERROR("Phantom registration failed!");
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::PrintProgressbar(100); 

  // Save result
  phantomRegistration->WriteConfiguration(configRootElement);
  vtkstd::string registrationResultFileName = "PhantomRegistrationTest.xml";
  vtksys::SystemTools::RemoveFile(registrationResultFileName.c_str());
  configRootElement->PrintXML(registrationResultFileName.c_str());

  if ( CompareRegistrationResultsWithBaseline( inputBaselineFileName.c_str(), registrationResultFileName.c_str() ) !=0 )
  {
    LOG_ERROR("Comparison of calibration data to baseline failed");
    std::cout << "Exit failure!!!" << std::endl; 
    return EXIT_FAILURE;
  }

  std::cout << "Exit success!!!" << std::endl; 
  return EXIT_SUCCESS; 
}

//-----------------------------------------------------------------------------

// return the number of differences
int CompareRegistrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName)
{
  int numberOfFailures=0;

  double* transformCurrent = new double[16]; 
  double* transformBaseline = new double[16]; 
  for (int i=0; i<16; ++i) {
    transformCurrent[i] = 0.0;
    transformBaseline[i] = 0.0;
  }

  // Load current phantom registration
  vtkSmartPointer<vtkXMLDataElement> currentRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(
    vtkXMLUtilities::ReadElementFromFile(currentResultFileName));
  if (currentRootElem == NULL) {  
    LOG_ERROR("Unable to read the current configuration file: " << currentResultFileName); 
    return ++numberOfFailures;
  }
  vtkXMLDataElement* phantomDefinitionCurrent = currentRootElem->LookupElementWithName("PhantomDefinition");
  if (phantomDefinitionCurrent == NULL) {
    LOG_ERROR("No phantom definition section is found in test result!");
    return ++numberOfFailures;
  }
  vtkXMLDataElement* geometryCurrent = phantomDefinitionCurrent->FindNestedElementWithName("Geometry"); 
  if (geometryCurrent == NULL) {
    LOG_ERROR("Phantom geometry information not found in test result!");
    return ++numberOfFailures;
  }
  vtkXMLDataElement* registrationCurrent = geometryCurrent->FindNestedElementWithName("Registration"); 
  if (registrationCurrent == NULL) {
    LOG_ERROR("Registration element not found in test result!");
    return ++numberOfFailures;
  } else {
    registrationCurrent->GetVectorAttribute("Transform", 16, transformCurrent);
  }

  // Load baseline phantom registration
  vtkSmartPointer<vtkXMLDataElement> baselineRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(
    vtkXMLUtilities::ReadElementFromFile(baselineFileName));
  if (baselineFileName == NULL) {  
    LOG_ERROR("Unable to read the baseline configuration file: " << baselineFileName); 
    return ++numberOfFailures;
  }
  vtkXMLDataElement* phantomDefinitionBaseline = baselineRootElem->LookupElementWithName("PhantomDefinition");
  if (phantomDefinitionBaseline == NULL) {
    LOG_ERROR("No phantom definition section is found in baseline!");
    return ++numberOfFailures;
  }
  vtkXMLDataElement* geometryBaseline = phantomDefinitionBaseline->FindNestedElementWithName("Geometry"); 
  if (geometryBaseline == NULL) {
    LOG_ERROR("Phantom geometry information not found in baseline!");
    return ++numberOfFailures;
  }
  vtkXMLDataElement* registrationBaseline = geometryBaseline->FindNestedElementWithName("Registration"); 
  if (registrationBaseline == NULL) {
    LOG_ERROR("Registration element not found in baseline!");
    return ++numberOfFailures;
  } else {
    registrationBaseline->GetVectorAttribute("Transform", 16, transformBaseline);
  }

  // Compare the transforms
  for (int i=0; i<16; ++i) {
    double ratio = 1.0 * transformCurrent[i] / transformBaseline[i];
    double diff = fabs(transformCurrent[i] - transformBaseline[i]);

    if ( (ratio > 1 + ERROR_THRESHOLD || ratio < 1 - ERROR_THRESHOLD) && (diff > 10.0 * ERROR_THRESHOLD) ) // error has to be greater than 5% and also greater than 0.5mm
    {
      LOG_ERROR("Transform element (" << i << ") mismatch: current=" << transformCurrent[i]<< ", baseline=" << transformBaseline[i]);
      numberOfFailures++;
    }
  }

  delete[] transformCurrent;
  delete[] transformBaseline;

  return numberOfFailures;
}
