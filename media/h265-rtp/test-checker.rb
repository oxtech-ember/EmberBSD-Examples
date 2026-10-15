#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors.

# Mutate a real, lossless producer capture. Mutations are negative controls.
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'

abort 'Usage: test-checker.rb LOSSLESS_RX_CAPTURE NEW_OUTPUT_DIRECTORY' unless ARGV.length == 2
capture_path, directory = ARGV
abort 'capture must be a regular file smaller than 8 MiB' unless
  File.file?(capture_path) && File.size(capture_path).between?(8, 8_388_608)
abort 'output directory already exists' if File.exist?(directory)
FileUtils.mkdir_p(directory)
original = File.binread(capture_path)
abort 'invalid capture header' unless original.byteslice(0, 8) == 'H265RTP1'
records = []
position = 8
while position < original.bytesize
  abort 'truncated record' unless original.bytesize - position >= 24
  length = original.byteslice(position, 4).unpack1('N')
  abort 'invalid datagram length' unless length.between?(12, 65_535) &&
    position + 24 + length <= original.bytesize
  records << {header: position, packet: position + 24, length: length}
  position += 24 + length
end
abort 'test requires at least two packets' unless records.length >= 2
payload_offset = lambda do |record|
  packet = original.byteslice(record[:packet], record[:length])
  offset = 12 + 4 * (packet.getbyte(0) & 15)
  offset += 4 + 4 * packet.byteslice(offset + 2, 2).unpack1('n') if (packet.getbyte(0) & 16) != 0
  record[:packet] + offset
end
fu_record = records.find { |record| (original.getbyte(payload_offset.call(record)) >> 1) & 63 == 49 }
abort 'test requires real FU packets' unless fu_record
fu_continuation = records[records.index(fu_record) + 1]
abort 'test requires a complete multi-packet FU' unless fu_continuation &&
  ((original.getbyte(payload_offset.call(fu_continuation)) >> 1) & 63) == 49
markers = records.select { |record| (original.getbyte(record[:packet] + 1) & 128) != 0 }
abort 'test requires at least two frame markers' unless markers.length >= 2
checker = File.join(__dir__, 'check-rtp.rb')
commands = []
results = []
check = lambda do |name, bytes, expect_success|
  path = File.join(directory, "#{name}.rtp")
  File.binwrite(path, bytes)
  command = [RbConfig.ruby, checker, path, '--events', 'received']
  stdout, stderr, status = Open3.capture3(*command)
  commands << command
  File.write(File.join(directory, "#{name}.stdout"), stdout)
  File.write(File.join(directory, "#{name}.stderr"), stderr)
  valid = expect_success ? status.success? : status.exitstatus == 1 && stderr.start_with?('check-rtp:')
  abort "unexpected checker result for #{name}: #{status.exitstatus}: #{stderr}" unless valid
  results << {name: name, expected_success: expect_success, exit: status.exitstatus}
  unless expect_success || name == 'sequence-gap'
    lossy_command = command + ['--allow-loss']
    lossy_stdout, lossy_stderr, lossy_status = Open3.capture3(*lossy_command)
    commands << lossy_command
    File.write(File.join(directory, "#{name}-allow-loss.stdout"), lossy_stdout)
    File.write(File.join(directory, "#{name}-allow-loss.stderr"), lossy_stderr)
    abort "loss mode accepted malformed #{name}" unless lossy_status.exitstatus == 1 &&
      lossy_stderr.start_with?('check-rtp:')
    results << {name: "#{name}-allow-loss", expected_success: false, exit: lossy_status.exitstatus}
  end
end
check.call('original', original, true)
mutate = lambda do |name, &block|
  bytes = original.dup
  block.call(bytes)
  check.call(name, bytes, false)
end
first = records.first
second = records[1]
mutate.call('wrong-magic') { |s| s.setbyte(0, 0) }
mutate.call('wrong-direction') { |s| s[first[:header] + 8, 4] = [2].pack('N') }
mutate.call('reserved-field') { |s| s[first[:header] + 12, 4] = [1].pack('N') }
mutate.call('oversized-datagram') { |s| s[first[:header], 4] = [65_536].pack('N') }
mutate.call('ordinal-gap') { |s| s[second[:header] + 4, 4] = [3].pack('N') }
mutate.call('reversed-clock') { |s| s[first[:header] + 16, 8] = [0xffff_ffff_ffff_ffff].pack('Q>') }
mutate.call('rtp-version') { |s| s.setbyte(first[:packet], s.getbyte(first[:packet]) & 63) }
mutate.call('payload-type') { |s| s.setbyte(first[:packet] + 1, s.getbyte(first[:packet] + 1) ^ 1) }
mutate.call('ssrc-change') { |s| s.setbyte(second[:packet] + 8, s.getbyte(second[:packet] + 8) ^ 1) }
mutate.call('sequence-gap') do |s|
  sequence = s.byteslice(second[:packet] + 2, 2).unpack1('n')
  s[second[:packet] + 2, 2] = [(sequence + 7) & 65_535].pack('n')
end
mutate.call('extension-overrun') do |s|
  s.setbyte(first[:packet], s.getbyte(first[:packet]) | 16)
  offset = first[:packet] + 12 + 4 * (s.getbyte(first[:packet]) & 15)
  s[offset + 2, 2] = [65_535].pack('n')
end
mutate.call('zero-padding') do |s|
  s.setbyte(first[:packet], s.getbyte(first[:packet]) | 32)
  s.setbyte(first[:packet] + first[:length] - 1, 0)
end
mutate.call('forbidden-bit') { |s| s.setbyte(payload_offset.call(first), s.getbyte(payload_offset.call(first)) | 128) }
mutate.call('zero-temporal-id') { |s| s.setbyte(payload_offset.call(first) + 1, 0) }
mutate.call('ap-length-overrun') do |s|
  offset = payload_offset.call(fu_record)
  s.setbyte(offset, (s.getbyte(offset) & 129) | (48 << 1))
  s[offset + 2, 2] = [65_535].pack('n')
end
mutate.call('fu-start-and-end') do |s|
  offset = payload_offset.call(fu_record) + 2
  s.setbyte(offset, s.getbyte(offset) | 192)
end
mutate.call('fu-timestamp-change') do |s|
  offset = fu_continuation[:packet] + 4
  timestamp = s.byteslice(offset, 4).unpack1('N')
  s[offset, 4] = [(timestamp + 1) & 0xffff_ffff].pack('N')
end
mutate.call('fu-nal-header-change') do |s|
  offset = payload_offset.call(fu_continuation) + 2
  s.setbyte(offset, s.getbyte(offset) ^ 1)
end
mutate.call('repeated-frame-timestamp') do |s|
  s[markers[1][:packet] + 4, 4] = s.byteslice(markers[0][:packet] + 4, 4)
end
check.call('truncated-record', original.byteslice(0, original.bytesize - 1), false)
check.call('truncated-record-header', original.byteslice(0, 12), false)
File.write(File.join(directory, 'commands.json'), JSON.pretty_generate(commands) + "\n")
File.write(File.join(directory, 'results.json'), JSON.pretty_generate(results) + "\n")
puts "PASS original capture and #{results.length - 1} malformed controls"
