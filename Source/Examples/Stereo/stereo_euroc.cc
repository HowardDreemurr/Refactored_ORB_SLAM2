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
#include <iomanip>
#include <iostream>

#include <opencv2/core/core.hpp>

#include <System.h>

namespace fs = ::boost::filesystem;
using namespace ::std;

void LoadImages(const string &strPathLeft, const string &strPathRight,
                const string &strPathTimes, vector<string> &vstrImageLeft,
                vector<string> &vstrImageRight, vector<double> &vTimeStamps);

int main(int argc, char **argv) {
  if (argc != 5) {
    cerr << endl
         << "Usage: ./stereo_euroc path_to_settings path_to_left_folder "
            "path_to_right_folder path_to_times_file"
         << endl;
    return 1;
  }

  // Retrieve paths to images
  vector<string> vstrImageLeft;
  vector<string> vstrImageRight;
  vector<double> vTimeStamp;
  LoadImages(string(argv[2]), string(argv[3]), string(argv[4]), vstrImageLeft,
             vstrImageRight, vTimeStamp);

  if (vstrImageLeft.empty() || vstrImageRight.empty()) {
    cerr << "ERROR: No images in provided path." << endl;
    return 1;
  }

  if (vstrImageLeft.size() != vstrImageRight.size()) {
    cerr << "ERROR: Different number of left and right images." << endl;
    return 1;
  }

  // Read rectification parameters

  // Settings
  string settingsFile =
      string(DEFAULT_STEREO_SETTINGS_DIR) + string("/") + string(argv[1]);

  cv::FileStorage fsSettings(settingsFile, cv::FileStorage::READ);
  if (!fsSettings.isOpened()) {
    cerr << "ERROR: Wrong path to settings" << endl;
    return -1;
  }

  cv::Mat K_l, K_r, P_l, P_r, R_l, R_r, D_l, D_r;
  fsSettings["LEFT.K"] >> K_l;
  fsSettings["RIGHT.K"] >> K_r;

  fsSettings["LEFT.P"] >> P_l;
  fsSettings["RIGHT.P"] >> P_r;

  fsSettings["LEFT.R"] >> R_l;
  fsSettings["RIGHT.R"] >> R_r;

  fsSettings["LEFT.D"] >> D_l;
  fsSettings["RIGHT.D"] >> D_r;

  int rows_l = fsSettings["LEFT.height"];
  int cols_l = fsSettings["LEFT.width"];
  int rows_r = fsSettings["RIGHT.height"];
  int cols_r = fsSettings["RIGHT.width"];

  if (K_l.empty() || K_r.empty() || P_l.empty() || P_r.empty() || R_l.empty() ||
      R_r.empty() || D_l.empty() || D_r.empty() || rows_l == 0 || rows_r == 0 ||
      cols_l == 0 || cols_r == 0) {
    cerr << "ERROR: Calibration parameters to rectify stereo are missing!"
         << endl;
    return -1;
  }

  cv::Mat M1l, M2l, M1r, M2r;
  cv::initUndistortRectifyMap(K_l, D_l, R_l, P_l.rowRange(0, 3).colRange(0, 3),
                              cv::Size(cols_l, rows_l), CV_32F, M1l, M2l);
  cv::initUndistortRectifyMap(K_r, D_r, R_r, P_r.rowRange(0, 3).colRange(0, 3),
                              cv::Size(cols_r, rows_r), CV_32F, M1r, M2r);

  const int nImages = vstrImageLeft.size();

  // Load both ORB and GCN vocabulary file whether or not "USE_ORB" is detected
  //const int Ntype = 1;
  //string vocabularyFile[Ntype];

  //vocabularyFile[0] = DEFAULT_BINARY_ORB_VOCABULARY;
  //vocabularyFile[1] = DEFAULT_BINARY_ORB_VOCABULARY;

  // Create SLAM system. It initializes all system threads and gets ready to
  // process frames.
  ORB_SLAM2::System SLAM(settingsFile, ORB_SLAM2::System::STEREO, true);

  // Vector for tracking time statistics
  vector<float> vTimesTrack;
  vTimesTrack.resize(nImages);

  cout << endl << "-------" << endl;
  cout << "Start processing sequence ..." << endl;
  cout << "Images in the sequence: " << nImages << endl << endl;

  // Main loop
  int main_error = 0;
  std::thread runthread([&]() { // Start in new thread
    cv::Mat imLeft, imRight, imLeftRect, imRightRect;
    for (int ni = 0; ni < nImages; ni++) {
      // Read left and right images from file
      imLeft = cv::imread(vstrImageLeft[ni], cv::IMREAD_UNCHANGED);
      imRight = cv::imread(vstrImageRight[ni], cv::IMREAD_UNCHANGED);

      if (imLeft.empty()) {
        cerr << endl
             << "Failed to load image at: " << string(vstrImageLeft[ni])
             << endl;
        main_error = 1;
        break;
      }

      if (imRight.empty()) {
        cerr << endl
             << "Failed to load image at: " << string(vstrImageRight[ni])
             << endl;
        main_error = 1;
        break;
      }

      if (SLAM.isFinished() == true) {
        break;
      }

      cv::remap(imLeft, imLeftRect, M1l, M2l, cv::INTER_LINEAR);
      cv::remap(imRight, imRightRect, M1r, M2r, cv::INTER_LINEAR);

      double tframe = vTimeStamp[ni];

      std::chrono::steady_clock::time_point t1 =
          std::chrono::steady_clock::now();

      // Pass the images to the SLAM system
      SLAM.TrackStereo(imLeftRect, imRightRect, tframe);

      std::chrono::steady_clock::time_point t2 =
          std::chrono::steady_clock::now();

      double ttrack =
          std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1)
              .count();

      vTimesTrack[ni] = ttrack;

      // Wait to load the next frame
      double T = 0;
      if (ni < nImages - 1)
        T = vTimeStamp[ni + 1] - tframe;
      else if (ni > 0)
        T = tframe - vTimeStamp[ni - 1];

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
  cout << "System Shutdown" << endl;

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

  return 0;
}

void LoadImages(const string &strPathLeft, const string &strPathRight,
                const string &strPathTimes, vector<string> &vstrImageLeft,
                vector<string> &vstrImageRight, vector<double> &vTimeStamps) {

  // Check the file exists
  if (fs::exists(strPathTimes) == false) {
    cerr << "FATAL: Could not find the timestamp file " << strPathTimes << endl;
    exit(0);
  }

  ifstream fTimes;
  fTimes.open(strPathTimes.c_str());
  vTimeStamps.reserve(5000);
  vstrImageLeft.reserve(5000);
  vstrImageRight.reserve(5000);
  while (!fTimes.eof()) {
    string s;
    getline(fTimes, s);
    if (!s.empty()) {
      stringstream ss;
      ss << s;
      vstrImageLeft.push_back(strPathLeft + "/" + ss.str() + ".png");
      vstrImageRight.push_back(strPathRight + "/" + ss.str() + ".png");
      double t;
      ss >> t;
      vTimeStamps.push_back(t / 1e9);
    }
  }
}
