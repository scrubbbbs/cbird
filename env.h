#pragma once

/// Get information about the operating system / environment
class Env {
 public:
  static void systemMemory(float& totalKb, float& freeKb);
  static void memoryUsage(float& vmKb, float& workingSetKb);

 private:
  Env();
  virtual ~Env();
};
