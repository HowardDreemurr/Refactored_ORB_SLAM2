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
#ifndef INITIALIZER_H
#define INITIALIZER_H

#include "Frame.h"
#include <opencv2/opencv.hpp>

namespace ORB_SLAM2 {

// THIS IS THE INITIALIZER FOR MONOCULAR SLAM. NOT USED IN THE STEREO OR RGBD
// CASE.
class Initializer {
  typedef std::pair<int, int> Match;

public:
  int Ntype;

public:
  // Fix the reference frame
  Initializer(const Frame &ReferenceFrame, int Ntype, float sigma = 1.0, int iterations = 200);

  // Computes in parallel a fundamental matrix and a homography. Selects a model and tries to recover the motion and the structure from motion
  bool Initialize(const Frame &CurrentFrame, const std::vector<std::vector<int>> &vMatches12, cv::Mat &R21, cv::Mat &t21, std::vector<std::vector<cv::Point3f>> &vP3D, std::vector<std::vector<bool>> &vbTriangulated);
  bool Initialize2(const Frame &CurrentFrame, const std::vector<std::vector<int>> &vMatches12, cv::Mat &R21, cv::Mat &t21, std::vector<std::vector<cv::Point3f>> &vP3D, std::vector<std::vector<bool>> &vbTriangulated);

private:
  void FindHomography(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                      std::vector<bool> &vbMatchesInliers, float &score, cv::Mat &H21, std::vector<std::vector<std::size_t>> vSets);

  void FindFundamental(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                       std::vector<bool> &vbInliers, float &score, cv::Mat &F21, std::vector<std::vector<std::size_t>> vSets);

  cv::Mat ComputeH21(const std::vector<cv::Point2f> &vP1, const std::vector<cv::Point2f> &vP2);

  cv::Mat ComputeF21(const std::vector<cv::Point2f> &vP1, const std::vector<cv::Point2f> &vP2);

  float CheckHomography(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                        const cv::Mat &H21, const cv::Mat &H12, std::vector<bool> &vbMatchesInliers, float sigma);

  float CheckFundamental(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                         const cv::Mat &F21, std::vector<bool> &vbMatchesInliers, float sigma);

  bool ReconstructF(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                    std::vector<bool> &vbMatchesInliers, cv::Mat &F21, cv::Mat &K, cv::Mat &R21, cv::Mat &t21, std::vector<cv::Point3f> &vP3D,
                    std::vector<bool> &vbTriangulated, float minParallax, int minTriangulated);

  bool ReconstructH(const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2, const std::vector<Match> &vMatches12,
                    std::vector<bool> &vbMatchesInliers, cv::Mat &H21, cv::Mat &K, cv::Mat &R21, cv::Mat &t21, std::vector<cv::Point3f> &vP3D,
                    std::vector<bool> &vbTriangulated, float minParallax, int minTriangulated);

  void Triangulate(const cv::KeyPoint &kp1, const cv::KeyPoint &kp2, const cv::Mat &P1, const cv::Mat &P2, cv::Mat &x3D);

  void Normalize(const std::vector<cv::KeyPoint> &vKeys, std::vector<cv::Point2f> &vNormalizedPoints, cv::Mat &T);

  int CheckRT(const cv::Mat &R, const cv::Mat &t, const std::vector<cv::KeyPoint> &vKeys1, const std::vector<cv::KeyPoint> &vKeys2,
              const std::vector<Match> &vMatches12, std::vector<bool> &vbInliers, const cv::Mat &K, std::vector<cv::Point3f> &vP3D, float th2,
              std::vector<bool> &vbGood, float &parallax);

  void DecomposeE(const cv::Mat &E, cv::Mat &R1, cv::Mat &R2, cv::Mat &t);

  // Keypoints from Reference Frame (Frame 1)
  std::vector<std::vector<cv::KeyPoint>> mvKeys1;

  // Keypoints from Current Frame (Frame 2)
  std::vector<std::vector<cv::KeyPoint>> mvKeys2;

  // Current Matches from Reference to Current
  std::vector<std::vector<Match>> mvMatches12;
  std::vector<std::vector<bool>> mvbMatched1;

  // Calibration
  cv::Mat mK;

  // Standard Deviation and Variance
  float mSigma, mSigma2;

  // Ransac max iterations
  int mMaxIterations;

  // Ransac sets
  std::vector<std::vector<std::vector<std::size_t>>> mvSets;
};

} // namespace ORB_SLAM2

#endif // INITIALIZER_H
