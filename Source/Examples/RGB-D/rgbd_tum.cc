/**
 * This file is part of ORB-SLAM2.
 *
 * Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University
 * of Zaragoza) For more information see <https://github.com/raulmur/ORB_SLAM2>
 *
 * ORB-SLAM2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ORB-SLAM2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

#include <opencv2/core/core.hpp>

#include <System.h>

namespace fs = ::boost::filesystem;
using namespace ::std;

void LoadImages(const string &strAssociationFilename,
                vector<string> &vstrImageFilenamesRGB,
                vector<string> &vstrImageFilenamesD,
                vector<double> &vTimestamps);

int main(int argc, char **argv) {
  if (argc != 4) {
    cerr << endl
         << "Usage: ./rgbd_tum path_to_settings path_to_sequence "
            "association_file_name"
         << endl;
    return 1;
  }

  // Retrieve paths to images
  vector<string> vstrImageFilenamesRGB;
  vector<string> vstrImageFilenamesD;
  vector<double> vTimestamps;
  string settingsFile = string(DEFAULT_RGBD_SETTINGS_DIR) + string("/") + string(argv[1]);
  string strAssociationFilename = string(DEFAULT_RGBD_SETTINGS_DIR) + "/associations/" + string(argv[3]);

  LoadImages(strAssociationFilename, vstrImageFilenamesRGB, vstrImageFilenamesD, vTimestamps);

  // Check consistency in the number of images and depthmaps
  int nImages = vstrImageFilenamesRGB.size();
  if (vstrImageFilenamesRGB.empty()) {
    cerr << endl << "No images found in provided path." << endl;
    return 1;
  } else if (vstrImageFilenamesD.size() != vstrImageFilenamesRGB.size()) {
    cerr << endl << "Different number of images for rgb and depth." << endl;
    return 1;
  }

  // Create SLAM system. It initializes all system threads and gets ready to process frames.

  // Load both ORB and GCN vocabulary file whether or not "USE_ORB" is detected  
  //const int Ntype = 1;
  //string vocabularyFile[Ntype];

  //vocabularyFile[0] = DEFAULT_BINARY_ORB_VOCABULARY;
  //vocabularyFile[1] = DEFAULT_BINARY_ORB_VOCABULARY;
  
  ORB_SLAM2::System SLAM(settingsFile, ORB_SLAM2::System::RGBD, true);

  // Vector for tracking time statistics
  vector<float> vTimesTrack;
  vTimesTrack.resize(nImages);

  cout << endl << "-------" << endl;
  cout << "Start processing sequence ..." << endl;
  cout << "Images in the sequence: " << nImages << endl << endl;

  // Main loop
  int main_error = 0;
  std::thread runthread([&]() { // Start in new thread
    cv::Mat imRGB, imD;
    for (int ni = 0; ni < nImages; ni++) {

      // Read image and depthmap from file
      imRGB = cv::imread(string(argv[2]) + "/" + vstrImageFilenamesRGB[ni], cv::IMREAD_UNCHANGED);
      imD = cv::imread(string(argv[2]) + "/" + vstrImageFilenamesD[ni], cv::IMREAD_UNCHANGED);
      double tframe = vTimestamps[ni];

      if (imRGB.empty()) {
        cerr << endl
             << "Failed to load image at: " << string(argv[2]) << "/"
             << vstrImageFilenamesRGB[ni] << endl;
        main_error = 1;
        break;
      }

      if (SLAM.isFinished() == true) {
        break;
      }

      std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

      // Pass the image to the SLAM system

      SLAM.TrackRGBD(imRGB, imD, tframe);

      std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();

      double ttrack = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1).count();

      vTimesTrack[ni] = ttrack;

      // Wait to load the next frame
      double T = 0;
      if (ni < nImages - 1)
        T = vTimestamps[ni + 1] - tframe;
      else if (ni > 0)
        T = tframe - vTimestamps[ni - 1];

      if (ttrack < T)
        this_thread::sleep_for(chrono::duration<double>(T - ttrack));      
    }
    SLAM.StopViewer();
  });

  // Start the visualization thread; this blocks until the SLAM system
  // has finished.
  SLAM.StartViewer();

  runthread.join();

  if (main_error != 0)
    return main_error;

  // Stop all threads
  SLAM.Shutdown();

  // Tracking time statistics
  sort(vTimesTrack.begin(), vTimesTrack.end());
  float totaltime = 0;
  for (int ni = 0; ni < nImages; ni++) {
    totaltime += vTimesTrack[ni];
  }
  cout << "-------" << endl << endl;
  cout << "median tracking time: " << vTimesTrack[nImages / 2] << endl;
  cout << "mean tracking time: " << totaltime / nImages << endl;

  // Save camera trajectory
  SLAM.SaveTrajectoryTUM("CameraTrajectory.txt");
  SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

  return 0;
}

void LoadImages(const string &strAssociationFilename, vector<string> &vstrImageFilenamesRGB, vector<string> &vstrImageFilenamesD, vector<double> &vTimestamps) {

  // Check the file exists
  if (fs::exists(strAssociationFilename) == false) {
    cerr << "FATAL: Could not find the associations file "
         << strAssociationFilename << endl;
    exit(0);
  }

  ifstream fAssociation;
  fAssociation.open(strAssociationFilename.c_str());
  while (!fAssociation.eof()) {
    string s;
    getline(fAssociation, s);
    if (!s.empty()) {
      stringstream ss;
      ss << s;
      double t;
      string sRGB, sD;
      ss >> t;
      vTimestamps.push_back(t);
      ss >> sRGB;
      vstrImageFilenamesRGB.push_back(sRGB);
      ss >> t;
      ss >> sD;
      vstrImageFilenamesD.push_back(sD);
    }
  }
}
