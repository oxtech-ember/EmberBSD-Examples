#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'json'
require 'digest'
require 'fileutils'

binary, fixtures, signature, output = ARGV
abort 'Usage: negative.rb VIEWER FIXTURES WIDTHxHEIGHT:FRAMES:FPS NEW_OUT' unless ARGV.length == 4
binary, fixtures = [binary, fixtures].map { |p| File.realpath(p) }
output = File.expand_path(output)
abort 'Output exists' if File.exist?(output) || File.symlink?(output)
Dir.mkdir(output)
inputs = (0..3).map { |i| fixtures + '/channel' + i.to_s + '.mp4' }
hashes = inputs.to_h { |p| [p, Digest::SHA256.file(p).hexdigest] }
corrupt = output + '/truncated.mp4'
File.binwrite(corrupt, File.binread(inputs[0], 32))
cases = [
  ['wrong-channel', [inputs[1], inputs[0], inputs[2], inputs[3]], 6],
  ['truncated', [corrupt, *inputs.drop(1)], 3],
  ['missing-file', [output + '/absent.mp4', *inputs.drop(1)], 2],
  ['existing-output', inputs, 2]
]
records = []
begin
  cases.each_with_index do |(name, paths, expected), index|
    root = output + '/' + name
    Dir.mkdir(root)
    destination = root + '/viewer'
    if name == 'existing-output'
      Dir.mkdir(destination)
      File.write(destination + '/marker', "Preserve this existing output.\n")
    end
    runtime = root + '/runtime'
    Dir.mkdir(runtime, 0700)
    env = { 'QT_QPA_PLATFORM' => 'vnc:port=' + (15917 + index).to_s + ':size=1280x800',
            'QT_MEDIA_BACKEND' => 'ffmpeg', 'QT_FFMPEG_DECODING_HW_DEVICE_TYPES' => ',',
            'XDG_RUNTIME_DIR' => runtime }
    args = [binary, '--accept', destination, '--timeout-ms', '120000', '--verify-fixtures', signature, '--require-all-frames', *paths]
    if ENV['MEMCHECK'] == 'yes'
      args = ['valgrind', '--tool=memcheck', '--leak-check=full', '--show-leak-kinds=all',
              '--errors-for-leak-kinds=definite,indirect,possible', '--track-origins=yes',
              '--error-exitcode=99', '-s', '--log-file=' + root + '/memcheck.log', *args]
    end
    status = nil
    File.open(root + '/viewer.log', 'w') do |log|
      pid = Process.spawn(env, *args, out: log, err: [:child, :out])
      status = Process.wait2(pid)[1]
    end
    record = { name: name, command: args, environment: env, expected_exit: expected,
               exit_status: status.exitstatus, signal: status.termsig }
    records << record
    raise 'Wrong failure status: ' + name unless status.exitstatus == expected
    if name == 'existing-output'
      raise 'Existing output changed' unless Dir.children(destination) == ['marker'] &&
        File.read(destination + '/marker') == "Preserve this existing output.\n"
    else
      report = JSON.parse(File.read(destination + '/report.json'))
      raise 'Failure was reported as success' unless report.fetch('exit_code') == expected && report.fetch('pass') == false
      record[:message] = report.fetch('message')
    end
    record[:passed] = true
  end
ensure
  preserved = hashes.all? { |p, sha| Digest::SHA256.file(p).hexdigest == sha }
  File.write(output + '/results.json', JSON.pretty_generate({ viewer_sha256: Digest::SHA256.file(binary).hexdigest,
    input_hashes: hashes, inputs_preserved: preserved, cases: records, passed: preserved && records.length == 4 && records.all? { |c| c[:passed] } }) + "\n")
end
puts 'Four real viewer refusal paths passed.'
