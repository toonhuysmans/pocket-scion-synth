#!/usr/bin/env swift

import CoreMIDI
import Foundation

final class MIDIReceiver {
    private let condition = NSCondition()
    private var current: [UInt8] = []
    private var frames: [[UInt8]] = []

    func receive(_ list: UnsafePointer<MIDIPacketList>) {
        var packet = list.pointee.packet
        for _ in 0..<list.pointee.numPackets {
            let bytes = withUnsafeBytes(of: packet.data) { raw in
                Array(raw.prefix(Int(packet.length)))
            }
            condition.lock()
            for byte in bytes {
                if byte == 0xf0 { current = [byte] }
                else if !current.isEmpty {
                    current.append(byte)
                    if byte == 0xf7 { frames.append(current); current = [] }
                }
            }
            condition.broadcast()
            condition.unlock()
            packet = MIDIPacketNext(&packet).pointee
        }
    }

    func wait(request: UInt8, timeout: TimeInterval = 2.0) throws -> [UInt8] {
        let deadline = Date().addingTimeInterval(timeout)
        condition.lock()
        defer { condition.unlock() }
        while true {
            if let index = frames.firstIndex(where: { $0.count > 8 && $0[6] == request }) {
                return frames.remove(at: index)
            }
            if !condition.wait(until: deadline) {
                throw NSError(domain: "PocketScionExport", code: 2,
                              userInfo: [NSLocalizedDescriptionKey: "MIDI request \(request) timed out"])
            }
        }
    }
}

func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value)
    return value?.takeRetainedValue() as String? ?? ""
}

func findEndpoint(count: Int, get: (Int) -> MIDIEndpointRef) -> MIDIEndpointRef? {
    for index in 0..<count {
        let endpoint = get(index)
        let name = endpointName(endpoint).lowercased()
        if name.contains("pocket scion") || name.contains("pra32") { return endpoint }
    }
    return nil
}

func checksum(_ bytes: [UInt8]) -> UInt8 {
    let sum = bytes.reduce(0) { ($0 + Int($1)) & 0x7f }
    return UInt8((128 - sum) & 0x7f)
}

var client = MIDIClientRef()
var inputPort = MIDIPortRef()
var outputPort = MIDIPortRef()
let receiver = MIDIReceiver()
guard MIDIClientCreate("Pocket SCION factory exporter" as CFString, nil, nil, &client) == noErr,
      MIDIInputPortCreateWithBlock(client, "Pocket SCION input" as CFString,
                                   &inputPort, { list, _ in receiver.receive(list) }) == noErr,
      MIDIOutputPortCreate(client, "Pocket SCION output" as CFString,
                           &outputPort) == noErr else {
    fatalError("Could not create CoreMIDI client")
}

guard let source = findEndpoint(count: MIDIGetNumberOfSources(), get: MIDIGetSource),
      let destination = findEndpoint(count: MIDIGetNumberOfDestinations(), get: MIDIGetDestination) else {
    fatalError("Pocket SCION MIDI source and destination were not found")
}
guard MIDIPortConnectSource(inputPort, source, nil) == noErr else {
    fatalError("Could not connect Pocket SCION MIDI input")
}

var requestID: UInt8 = 0
func request(command: UInt8, payload: [UInt8]) throws -> [UInt8] {
    requestID = (requestID + 1) & 0x7f
    let body: [UInt8] = [0x7d, 0x50, 0x53, 0x01, command, requestID] +
        payload.map { $0 & 0x7f }
    let message = [UInt8(0xf0)] + body + [checksum(body), 0xf7]
    var packetList = MIDIPacketList()
    let packet = MIDIPacketListInit(&packetList)
    _ = message.withUnsafeBufferPointer { buffer in
        MIDIPacketListAdd(&packetList, MemoryLayout<MIDIPacketList>.size,
                          packet, 0, message.count, buffer.baseAddress!)
    }
    guard MIDISend(outputPort, destination, &packetList) == noErr else {
        throw NSError(domain: "PocketScionExport", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "Could not send MIDI request"])
    }
    let response = try receiver.wait(request: requestID)
    guard response.count >= 11, response[5] != 0x7f else {
        throw NSError(domain: "PocketScionExport", code: 3,
                      userInfo: [NSLocalizedDescriptionKey: "Device rejected request"])
    }
    return response
}

func targetBytes(_ target: Int) -> [UInt8] {
    [UInt8(target & 0x7f), UInt8((target >> 7) & 0x7f)]
}

func readValue(scope: UInt8, target: Int, lane: Int, parameter: Int) throws -> Int {
    let response = try request(command: 0x03,
        payload: [scope] + targetBytes(target) + [UInt8(lane), UInt8(parameter)])
    return Int(response[response.count - 4]) | (Int(response[response.count - 3]) << 7)
}

let names = ["legacy", "foundation", "organic", "percussive", "bass-and-lead",
             "atmosphere", "spectral", "extreme"]
let output = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first ??
                 "presets/factory-v2.4", isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

for bank in 0..<8 {
    var settings: [Int] = []
    for parameter in 0..<19 {
        settings.append(try readValue(scope: 5, target: bank, lane: 0,
                                      parameter: parameter))
    }
    var patches: [[String: Any]] = []
    for program in 0..<16 {
        let patchID = bank * 16 + program
        var lanes: [[Int]] = []
        for lane in 0..<3 {
            var values: [Int] = []
            for parameter in 0..<47 {
                values.append(try readValue(scope: 4, target: patchID,
                                            lane: lane, parameter: parameter))
            }
            lanes.append(values)
        }
        var shared: [Int] = []
        for parameter in 0..<113 {
            shared.append(try readValue(scope: 4, target: patchID, lane: 3,
                                        parameter: parameter))
        }
        patches.append(["schema": "pocket-scion-patch", "version": 4,
                        "patchId": patchID, "lanes": lanes, "shared": shared])
        print("Bank \(bank): patch \(program + 1)/16", terminator: "\r")
        fflush(stdout)
    }
    let bankFile: [String: Any] = ["schema": "pocket-scion-bank", "version": 4,
        "bankIndex": bank, "settings": settings, "patches": patches]
    let data = try JSONSerialization.data(withJSONObject: bankFile,
                                           options: [.prettyPrinted, .sortedKeys])
    let file = output.appendingPathComponent(String(format: "bank-%02d-%@.json",
                                                     bank, names[bank]))
    try data.write(to: file, options: .atomic)
    print("Bank \(bank) written to \(file.path)                 ")
}

print("Factory banks 0–7 exported without reading flash overrides.")
