/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

/*!
  \file vtkPlusWinProbeVideoSourceTest.cxx
  \brief Test basic connection to the WinProbe ultrasound system
  and write some frames to output file(s)
  \ingroup PlusLibDataCollection
*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkSmartPointer.h"
#include "vtkPlusWinProbeVideoSource.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx"
#include "vtkImageViewer.h"
#include "vtkActor2D.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkPlusDataSource.h"

#include <vtkPlusDataCollector.h>
#include <vtkPlusVirtualCapture.h>

class vtkMyCallback : public vtkCommand
{
public:
  static vtkMyCallback* New() { return new vtkMyCallback; }

  virtual void Execute(vtkObject* caller, unsigned long, void*)
  {
    m_Viewer->Render();

    //update the timer so it will trigger again
    m_Interactor->CreateTimer(VTKI_TIMER_UPDATE);
  }

  vtkRenderWindowInteractor* m_Interactor;
  vtkImageViewer* m_Viewer;

private:

  vtkMyCallback()
  {
    m_Interactor = nullptr;
    m_Viewer = nullptr;
  }
};

int main(int argc, char* argv[])
{
  bool printHelp(false);
  bool renderingOff(false);
  std::string inputConfigFileName;
  std::string outputFileName("WinProbeOutputSeq.nrrd");
  int verboseLevel = vtkPlusLogger::LOG_LEVEL_DEBUG;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);
  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help.");
  args.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Config file containing the device configuration.");
  args.AddArgument("--rendering-off", vtksys::CommandLineArguments::NO_ARGUMENT, &renderingOff, "Run test without rendering.");
  args.AddArgument("--output-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputFileName, "Filename of the output video buffer sequence metafile (Default: VideoBufferMetafile)");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level 1=error only, 2=warning, 3=info, 4=debug, 5=trace)");

  if(!args.Parse())
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "\nvtkPlusWinProbeVideoSourceTest help:" << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);
  if(printHelp)
  {
    std::cout << "\n\nvtkPlusWinProbeVideoSourceTest help:" << args.GetHelp() << std::endl;
    exit(EXIT_SUCCESS);
  }

  // ==========================
  //LOG_WARNING("SLEEPING! ATTACH DEBUGGER TO THIS NOW!!!");
  //Sleep(20000);

  vtkPlusDevice* aDevice = nullptr;
  vtkPlusVirtualCapture* pVirtualDiscCapture[2] = {nullptr, nullptr};
  vtkSmartPointer<vtkPlusDataCollector> dataCollector = vtkSmartPointer<vtkPlusDataCollector>::New();

  // vtkSmartPointer< vtkPlusWinProbeVideoSource > WinProbeDevice = vtkSmartPointer< vtkPlusWinProbeVideoSource >::New();
  // WinProbeDevice->SetDeviceId("VideoDeviceLinearArray");
  vtkPlusWinProbeVideoSource* WinProbeDevice = nullptr;

  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if(STRCASECMP(inputConfigFileName.c_str(), "") != 0)
  {
    LOG_DEBUG("Reading config file...");

    if(PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, inputConfigFileName.c_str()) == PLUS_FAIL)
    {
      LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str());
      return EXIT_FAILURE;
    }

    //WinProbeDevice->ReadConfiguration(configRootElement);

    dataCollector->ReadConfiguration(configRootElement);
  }

  // std::cout << "\n" << *WinProbeDevice; //invokes PrintSelf()

  /*
  if(WinProbeDevice->Connect() != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to connect to WinProbe Probe");
    exit(EXIT_FAILURE);
  }
  */
  if (dataCollector->Connect() != PLUS_SUCCESS)
  {
    LOG_ERROR("data collector cannot connect");
    return EXIT_FAILURE;
  }

  if (dataCollector->GetDevice(aDevice, "VideoDeviceLinearArray") != PLUS_SUCCESS)
  {
      LOG_INFO("Unable to locate LinearArray device with Id=\"VideoDeviceLinearArray\".");
      return EXIT_FAILURE;
  }
  else
  {
      WinProbeDevice = dynamic_cast<vtkPlusWinProbeVideoSource*>(aDevice);
  }

  if (dataCollector->GetDevice(aDevice, "CaptureDeviceLinearArray") != PLUS_SUCCESS)
  {
      LOG_ERROR("No VirtualCapture has been found by the name CaptureDeviceLinearArray");
      return EXIT_FAILURE;
  }
  pVirtualDiscCapture[0] = vtkPlusVirtualCapture::SafeDownCast(aDevice);

  if (dataCollector->GetDevice(aDevice, "CaptureDeviceLinearArray2") != PLUS_SUCCESS)
  {
      LOG_ERROR("No VirtualCapture has been found by the name CaptureDeviceLinearArray2");
      return EXIT_FAILURE;
  }
  pVirtualDiscCapture[1] = vtkPlusVirtualCapture::SafeDownCast(aDevice);

  if (dataCollector->Start() != PLUS_SUCCESS)
  {
      LOG_ERROR("Datacollector failed to start");
      return EXIT_FAILURE;
  }


  /*
  //test starting and stopping (pausing recording)
  WinProbeDevice->StartRecording(); //applies the setting read from config file
  std::cout << "\n" << *WinProbeDevice; //invokes PrintSelf()
  WinProbeDevice->StopRecording();

  //test TGCs
  double tgc = WinProbeDevice->GetTimeGainCompensation(7);
  tgc = WinProbeDevice->GetTimeGainCompensation(3);
  WinProbeDevice->SetTimeGainCompensation(3, 0.2);
  tgc = WinProbeDevice->GetTimeGainCompensation(3);

  //test intensity compression
  uint16_t uVal = WinProbeDevice->GetLogLinearKnee();
  WinProbeDevice->SetLogLinearKnee(123);
  uVal = WinProbeDevice->GetLogLinearKnee();

  LOG_DEBUG("just making sure: enabled is: " << WinProbeDevice->GetARFIIsEnabled() << " ============================ ");
  // WinProbeDevice->StartRecording();
  */

  LOG_DEBUG("Opening files for virtual capture and enabling capture\n");
  std::string primaryFileName("C:\\Users\\AdamAji\\Documents\\testing\\wow_split_0.mha");
  std::string extraFileName("C:\\Users\\AdamAji\\Documents\\testing\\wow_split_0_rf.mha");
  // pVirtualDiscCapture[0]->OpenFile(primaryFileName.c_str());
  pVirtualDiscCapture[1]->OpenFile(extraFileName.c_str());
  // pVirtualDiscCapture[0]->SetEnableCapturing(true);
  // pVirtualDiscCapture[1]->SetEnableCapturing(true);

  if(renderingOff)
  {
    Sleep(5000);
    WinProbeDevice->ARFIPush(); // in case we are in ARFI mode, invoke it
    Sleep(20000); //allow some time to buffer frames

    /*
    vtkPlusChannel* bChannel(nullptr);
    if(WinProbeDevice->GetOutputChannelByName(bChannel, "VideoStream") != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to locate the channel with Id=\"VideoStream\". Check config file.");
      exit(EXIT_FAILURE);
    }

    vtkPlusChannel* rfChannel(nullptr);
    if(WinProbeDevice->GetOutputChannelByName(rfChannel, "RfStream") != PLUS_SUCCESS)
    {
      LOG_WARNING("Unable to locate the channel with Id=\"RFStream\". RF mode will not be used.");
    }
    */

    // pVirtualDiscCapture[1]->SetEnableCapturing(true);
    WinProbeDevice->FreezeDevice(true);

    LOG_DEBUG("Closing virtual capture files\n");
    // pVirtualDiscCapture[0]->SetEnableCapturing(false);
    pVirtualDiscCapture[1]->SetEnableCapturing(false);
    LOG_ERROR("ATTEMPTING TO MANUALLY TAKE VIRTUAL CAPTURE SNAPSHOT");
    // pVirtualDiscCapture[1]->TakeSnapshot();
    /*
    for (int i=0; i<30; i++) {
      LOG_ERROR("ATTEMPTING TO MANUALLY TAKE VIRTUAL CAPTURE SNAPSHOT");
      pVirtualDiscCapture[1]->TakeSnapshot();
    }
    */
    // pVirtualDiscCapture[0]->CloseFile();
    // pVirtualDiscCapture[1]->CloseFile();

    /*
    vtkPlusDataSource* bSource(nullptr);
    bChannel->GetVideoSource(bSource);
    bSource->WriteToSequenceFile(outputFileName.c_str());

    if(rfChannel)
    {
      vtkPlusDataSource* rfSource(nullptr);
      rfChannel->GetVideoSource(rfSource);
      rfSource->WriteToSequenceFile((outputFileName + "_RF.mha").c_str());
    }
    */

    //update and write configuration
    //WinProbeDevice->WriteConfiguration(configRootElement);
    dataCollector->WriteConfiguration(configRootElement);
    bool success = vtkXMLUtilities::WriteElementToFile(configRootElement, (outputFileName + ".xml").c_str());
    if(!success)
    {
      LOG_ERROR("Unable to write configuration to: " << outputFileName + ".xml");
    }
    else
    {
      LOG_INFO("Configuration file written to: " << outputFileName + ".xml");
    }

    WinProbeDevice->FreezeDevice(false);
  }
  else
  {
    vtkSmartPointer<vtkImageViewer> viewer = vtkSmartPointer<vtkImageViewer>::New();
    viewer->SetInputConnection(WinProbeDevice->GetOutputPort(0)); //set image to the render and window
    viewer->SetColorWindow(255);
    viewer->SetColorLevel(127.5);
    viewer->SetZSlice(0);
    viewer->SetSize(256, 640);

    //Create the interactor that handles the event loop
    vtkSmartPointer<vtkRenderWindowInteractor> iren = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    iren->SetRenderWindow(viewer->GetRenderWindow());
    viewer->SetupInteractor(iren);

    viewer->Render(); //must be called after iren and viewer are linked or there will be problems

    //Sleep(50);
    //WinProbeDevice->ARFIPush(); // in case we are in ARFI mode, invoke it

    // Establish timer event and create timer to update the live image
    vtkSmartPointer<vtkMyCallback> call = vtkSmartPointer<vtkMyCallback>::New();
    call->m_Interactor = iren;
    call->m_Viewer = viewer;
    iren->AddObserver(vtkCommand::TimerEvent, call);
    iren->CreateTimer(VTKI_TIMER_FIRST);

    //iren must be initialized so that it can handle events
    iren->Initialize();
    iren->Start();
  }

  /*
  WinProbeDevice->StopRecording();
  WinProbeDevice->Disconnect();
  */
  dataCollector->Stop();
  dataCollector->Disconnect();

  return EXIT_SUCCESS;
}
