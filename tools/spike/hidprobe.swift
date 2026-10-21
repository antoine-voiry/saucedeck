// Spike probe: open SAUCEDECK's vendor HID collection, send a report, wait for the echo.
// Run: swift tools/spike/hidprobe.swift   (no Input Monitoring permission granted)
import Foundation
import IOKit.hid

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
// macOS shows the composite HID interface as one device with primary usage = keyboard,
// so match any top-level collection (DeviceUsagePage), not the primary one.
let match: [String: Any] = [kIOHIDVendorIDKey: 0x303A, kIOHIDDeviceUsagePageKey: 0xFF00]
IOHIDManagerSetDeviceMatching(mgr, match as CFDictionary)
IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
let openResult = IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
print("IOHIDManagerOpen:", openResult == kIOReturnSuccess ? "OK" : String(format: "0x%08x", openResult))

guard let devices = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, let dev = devices.first else {
  print("FAIL: no vendor HID device (usage page 0xFF00) found"); exit(1)
}
let product = IOHIDDeviceGetProperty(dev, kIOHIDProductKey as CFString) as? String ?? "?"
print("device:", product)

var inBuf = [UInt8](repeating: 0, count: 64)
var echoed = false
let cb: IOHIDReportCallback = { _, _, _, _, reportID, report, length in
  let bytes = (0..<min(length, 4)).map { String(format: "%02X", report[$0]) }.joined(separator: " ")
  print("ECHO reportID=\(reportID) len=\(length): \(bytes)")
  echoed = true
}
IOHIDDeviceRegisterInputReportCallback(dev, &inBuf, inBuf.count, cb, nil)

var report = [UInt8](repeating: 0, count: 63)
report[0] = 1; report[1] = 0x01; report[2] = 1; report[3] = 1  // HELLO v1
for id in 1...6 where !echoed {
  let r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, CFIndex(id), report, report.count)
  print("SetReport id=\(id):", r == kIOReturnSuccess ? "OK" : String(format: "0x%08x", r))
  CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.5, false)
}
print(echoed ? "PASS: echo received" : "FAIL: no echo")
