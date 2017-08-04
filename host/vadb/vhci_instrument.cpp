/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <fstream>
#include <sstream>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "host/vadb/vhci_instrument.h"

namespace vadb {
namespace {
// Device ID is specified as a concatenated pair of BUS and DEVICE id.
// Since we only export one device and our server doesn't care much about
// its number, we use the default value of BUS=1 and DEVICE=1.
// This can be set to something else and should still work, as long as
// numbers are valid in USB sense.
constexpr uint32_t kDefaultDeviceID = (1 << 16) | 1;
constexpr uint32_t kDefaultDeviceSpeed = 2;
// Subsystem and device type where VHCI driver is located.
// These values can usually be found after loading vhci-hcd module here:
// /sys/devices/platform/vhci_hcd/modalias
constexpr char kVHCISubsystem[] = "platform";
constexpr char kVHCIDevType[] = "vhci_hcd";

// Port status values deducted from /sys/devices/platform/vhci_hcd/status
enum {
  // kVHCIPortFree indicates the port is not currently in use.
  kVHCIStatusPortFree = 4
};
}  // anonymous namespace

VHCIInstrument::VHCIInstrument()
    : udev_(nullptr, [](udev* u) { udev_unref(u); }),
      vhci_device_(nullptr,
                   [](udev_device* device) { udev_device_unref(device); }) {}

bool VHCIInstrument::Init() {
  udev_.reset(udev_new());
  CHECK(udev_) << "Could not create libudev context.";

  vhci_device_.reset(udev_device_new_from_subsystem_sysname(udev_.get(),
                                                            kVHCISubsystem,
                                                            kVHCIDevType));
  if (!vhci_device_) {
    LOG(ERROR) << "VHCI not available. Is the driver loaded?";
    LOG(ERROR) << "Try: sudo modprobe vhci_hcd";
    LOG(ERROR) << "The driver is part of linux-image-extra-`uname -r` package";
    return false;
  }

  syspath_ = udev_device_get_syspath(vhci_device_.get());

  if (!FindFreePort()) {
    LOG(ERROR) << "It appears all your VHCI ports are currently occupied.";
    LOG(ERROR) << "New VHCI device cannot be registered unless one of the "
               << "ports is freed.";
    return false;
  }

  attach_thread_.reset(new std::thread([this]() {
    AttachThread(); }));
  return true;
}

bool VHCIInstrument::FindFreePort() {
  std::ifstream stat(syspath_ + "/status");
  int port;
  int status;
  std::string everything_else;

  if (!stat.is_open()) {
    LOG(ERROR) << "Could not open usb-ip status file.";
    return false;
  }

  // Skip past the header line.
  std::getline(stat, everything_else);

  while (stat.rdstate() == std::ios_base::goodbit) {
    stat >> port >> status;
    std::getline(stat, everything_else);
    if (status == kVHCIStatusPortFree) {
      port_ = port;
      LOG(INFO) << "Using VHCI port " << port_;
      return true;
    }
  }
  return false;
}

void VHCIInstrument::AttachThread() {
  while (true) {
    sleep(3);
    if (Attach()) {
      LOG(INFO) << "Attach successful.";
      break;
    }
  }
}

bool VHCIInstrument::Attach() {
  struct addrinfo hints {};
  std::unique_ptr<addrinfo, void (*)(addrinfo*)> addr(nullptr, freeaddrinfo);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;

  {
    struct addrinfo* addr_raw;
    auto gaierr = getaddrinfo("::1", "3240", &hints, &addr_raw);
    if (gaierr != 0) {
      LOG(ERROR) << "getaddrinfo() failed: " << gai_strerror(gaierr);
      return false;
    }
    addr.reset(addr_raw);
  }

  // Hypothetically we could just use something else, here, too.
  // We use RAW sockets, because we need to pass RAW socket number to USB/IP.
  // Using avd::SharedFD would give no benefit - and would actually require me
  // to acquire a duplicate anyway.
  int connection =
      socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if (connection < 0) return false;

  if (connect(connection, addr->ai_addr, addr->ai_addrlen) < 0) {
    LOG(ERROR) << "Could not connect: " << strerror(errno);
    close(connection);
    return false;
  }

  std::stringstream result;
  result << port_ << ' ' << connection << ' ' << kDefaultDeviceID << ' ' <<
         kDefaultDeviceSpeed;
  std::ofstream attach(syspath_ + "/attach");

  if (!attach.is_open()) {
    LOG(WARNING) << "Could not open VHCI attach file.";
    return false;
  }

  LOG(INFO) << "Attaching USB/IP device using: " << result.str();
  attach << result.str();

  return attach.rdstate() == std::ios_base::goodbit;
}

}  // namespace vadb